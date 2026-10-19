// End-to-end integration for ExecPlan 0012 Step 8:
//   adapter-shaped source-sync payload -> ingestion -> public search
//   visibility -> disappearance (missing, then closed) -> reopen.
//
// The payload shape matches what the Python adapters emit (external_id,
// published_at, empty requirements, no salary). Timestamps are derived from
// MySQL NOW() so the test does not depend on the wall clock.

#include <arpa/inet.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "jobpilot/cache/session_manager.h"
#include "jobpilot/http/http_server.h"
#include "jobpilot/network/event_loop.h"
#include "jobpilot/service/auth/session_authenticator.h"
#include "jobpilot/service/job/internal_source_sync_controller.h"
#include "jobpilot/service/job/job_controller.h"
#include "jobpilot/service/job/job_dao.h"
#include "jobpilot/service/job/job_service.h"
#include "jobpilot/service/job/source_sync_dao.h"
#include "jobpilot/service/job/source_sync_service.h"
#include "jobpilot/service/router.h"
#include "jobpilot/service/user/user_controller.h"
#include "jobpilot/service/user/user_dao.h"
#include "jobpilot/service/user/user_service.h"
#include "test_job_support.h"
#include "test_user_support.h"
#include "http_test_timeout.h"

namespace jobpilot::service
{
    namespace
    {
        constexpr std::uint16_t kTestPort = 19098;

        struct ServerState
        {
            std::mutex mutex;
            std::condition_variable condition;
            jobpilot::core::EventLoop *loop = nullptr;
            bool ready = false;
            std::exception_ptr exception;
        };

        struct HttpResponseData
        {
            int statusCode = 0;
            std::string body;
            nlohmann::json jsonBody;
            bool hasJson = false;
        };

        bool SendAll(
            int socketFd,
            const std::string &data)
        {
            std::size_t sent = 0;

            while (sent < data.size())
            {
                const ssize_t result =
                    ::send(
                        socketFd,
                        data.data() + sent,
                        data.size() - sent,
                        0);

                if (result <= 0)
                {
                    return false;
                }

                sent += static_cast<std::size_t>(
                    result);
            }

            return true;
        }

        int CreateClientSocket()
        {
            const int socketFd =
                ::socket(AF_INET, SOCK_STREAM, 0);

            if (socketFd < 0)
            {
                return -1;
            }

            timeval timeout{};
            timeout.tv_sec = jobpilot::test::HttpTestTimeoutSeconds(3);

            if (::setsockopt(
                    socketFd,
                    SOL_SOCKET,
                    SO_RCVTIMEO,
                    &timeout,
                    sizeof(timeout)) != 0)
            {
                ::close(socketFd);
                return -1;
            }

            return socketFd;
        }

        bool ConnectToServer(int socketFd)
        {
            sockaddr_in serverAddress{};
            serverAddress.sin_family = AF_INET;
            serverAddress.sin_port = htons(kTestPort);

            return ::inet_pton(
                       AF_INET,
                       "127.0.0.1",
                       &serverAddress.sin_addr) == 1 &&
                   ::connect(
                       socketFd,
                       reinterpret_cast<sockaddr *>(
                           &serverAddress),
                       sizeof(serverAddress)) == 0;
        }

        std::optional<HttpResponseData> SendRequest(
            const std::string &method,
            const std::string &path,
            const std::string &body,
            const std::string &internalToken)
        {
            const int socketFd =
                CreateClientSocket();

            if (socketFd < 0 ||
                !ConnectToServer(socketFd))
            {
                if (socketFd >= 0)
                {
                    ::close(socketFd);
                }

                return std::nullopt;
            }

            std::string request =
                method + " " + path +
                " HTTP/1.1\r\n"
                "Host: 127.0.0.1\r\n"
                "Connection: close\r\n";

            if (!internalToken.empty())
            {
                request +=
                    "X-Internal-Token: " +
                    internalToken + "\r\n";
            }

            if (!body.empty())
            {
                request +=
                    "Content-Type: application/json\r\n"
                    "Content-Length: " +
                    std::to_string(body.size()) +
                    "\r\n";
            }

            request += "\r\n";
            request += body;

            if (!SendAll(socketFd, request))
            {
                ::close(socketFd);
                return std::nullopt;
            }

            std::string rawResponse;
            char buffer[4096];

            while (true)
            {
                const ssize_t received =
                    ::recv(
                        socketFd,
                        buffer,
                        sizeof(buffer),
                        0);

                if (received <= 0)
                {
                    break;
                }

                rawResponse.append(
                    buffer,
                    static_cast<std::size_t>(
                        received));
            }

            ::close(socketFd);

            const std::size_t headerEnd =
                rawResponse.find("\r\n\r\n");

            if (headerEnd == std::string::npos ||
                rawResponse.size() < 12)
            {
                return std::nullopt;
            }

            HttpResponseData response;
            response.statusCode =
                std::stoi(
                    rawResponse.substr(9, 3));
            response.body =
                rawResponse.substr(headerEnd + 4);

            try
            {
                response.jsonBody =
                    nlohmann::json::parse(
                        response.body);
                response.hasJson = true;
            }
            catch (const nlohmann::json::parse_error &)
            {
                response.hasJson = false;
            }

            return response;
        }

        // Adapter payload shape (what source_adapters emits for one job).
        nlohmann::json MakeAdapterJob(
            const std::string &token,
            const std::string &name,
            const std::string &sourceUrl,
            const std::string &publishedAt)
        {
            return nlohmann::json{
                {"company", "GitLab"},
                {"title", token + " " + name},
                {"source", "greenhouse"},
                {"source_url", sourceUrl},
                {"requirements", nlohmann::json::array()},
                {"location", "Remote, France"},
                {"description", "Build backend services."},
                {"external_id", "8860302002"},
                {"published_at", publishedAt}};
        }

        std::string MakeObservationBody(
            const std::string &scope,
            const std::string &observedAt,
            bool complete,
            const nlohmann::json &jobs)
        {
            return nlohmann::json{
                {"source", "greenhouse"},
                {"source_scope", scope},
                {"complete", complete},
                {"observed_at", observedAt},
                {"jobs", jobs}}
                .dump();
        }
    } // namespace

    class SourceSyncE2ETest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            pool = jobpilot::test::MakeConnectionPool();
            redis = jobpilot::test::MakeRedisClient();
            sessions =
                std::make_unique<
                    jobpilot::core::SessionManager>(
                    *redis);
            authenticator =
                std::make_unique<SessionAuthenticator>(
                    *sessions);
            userDao =
                std::make_unique<UserDao>(*pool);
            userService =
                std::make_unique<UserService>(
                    *userDao,
                    *sessions);
            userController =
                std::make_unique<UserController>(
                    *userService);

            jobDao = std::make_unique<JobDao>(*pool);
            jobService =
                std::make_unique<JobService>(*jobDao);
            jobController =
                std::make_unique<JobController>(
                    *jobService);

            sourceSyncDao =
                std::make_unique<SourceSyncDao>();
            sourceSyncService =
                std::make_unique<SourceSyncService>(
                    *sourceSyncDao,
                    *pool);

            internalToken =
                jobpilot::test::UniqueJobToken(
                    "e2e-token");
            internalController =
                std::make_unique<
                    InternalSourceSyncController>(
                    *sourceSyncService,
                    internalToken);

            token =
                jobpilot::test::UniqueJobToken(
                    "e2e-job");
            scope =
                jobpilot::test::UniqueJobToken(
                    "e2e-scope");

            serverThread = std::thread(
                [this]()
                {
                    try
                    {
                        jobpilot::core::EventLoop loop;
                        jobpilot::core::HttpServer server(
                            &loop,
                            "127.0.0.1",
                            kTestPort);

                        RegisterRoutes(
                            &server,
                            userController.get(),
                            nullptr,
                            jobController.get(),
                            nullptr,
                            nullptr,
                            internalController.get());

                        server.Start();

                        {
                            std::lock_guard<std::mutex> lock(
                                state.mutex);
                            state.loop = &loop;
                            state.ready = true;
                        }

                        state.condition.notify_one();
                        loop.Loop();
                        server.Stop();
                    }
                    catch (...)
                    {
                        {
                            std::lock_guard<std::mutex> lock(
                                state.mutex);
                            state.exception =
                                std::current_exception();
                            state.ready = true;
                        }

                        state.condition.notify_one();
                    }
                });

            std::unique_lock<std::mutex> lock(
                state.mutex);

            state.condition.wait(
                lock,
                [this]()
                {
                    return state.ready;
                });

            if (state.exception)
            {
                std::rethrow_exception(
                    state.exception);
            }
        }

        void TearDown() override
        {
            jobpilot::core::EventLoop *loop = nullptr;

            {
                std::lock_guard<std::mutex> lock(
                    state.mutex);
                loop = state.loop;
            }

            if (loop != nullptr)
            {
                loop->QueueInLoop(
                    [loop]()
                    {
                        loop->Quit();
                    });
            }

            if (serverThread.joinable())
            {
                serverThread.join();
            }

            const jobpilot::core::Result jobs =
                pool->Query(
                    "SELECT id FROM jobs WHERE source_scope = ?",
                    {scope});

            for (std::size_t row = 0;
                 row < jobs.rowCount();
                 ++row)
            {
                const auto &value = jobs.value(row, 0);

                if (value.has_value())
                {
                    jobpilot::test::CleanupJob(
                        pool.get(),
                        static_cast<std::uint64_t>(
                            std::stoull(value.value())));
                }
            }
        }

        std::string Timestamp(
            const std::string &expression)
        {
            const jobpilot::core::Result result =
                pool->Query(
                    "SELECT " + expression,
                    {});

            if (result.rowCount() != 1)
            {
                throw std::runtime_error(
                    "timestamp query returned no row");
            }

            return jobpilot::test::RequireValue(
                result.value(0, 0));
        }

        std::optional<HttpResponseData> Publish(
            const std::string &observedAt,
            bool complete,
            const nlohmann::json &jobs)
        {
            return SendRequest(
                "POST",
                "/internal/jobs/source-sync",
                MakeObservationBody(
                    scope,
                    observedAt,
                    complete,
                    jobs),
                internalToken);
        }

        std::string SearchUrl(
            const std::string &extra = std::string()) const
        {
            return "/api/v1/jobs?keyword=" + token + extra;
        }

        std::unique_ptr<jobpilot::core::ConnectionPool> pool;
        std::unique_ptr<jobpilot::core::RedisClient> redis;
        std::unique_ptr<jobpilot::core::SessionManager> sessions;
        std::unique_ptr<SessionAuthenticator> authenticator;
        std::unique_ptr<UserDao> userDao;
        std::unique_ptr<UserService> userService;
        std::unique_ptr<UserController> userController;
        std::unique_ptr<JobDao> jobDao;
        std::unique_ptr<JobService> jobService;
        std::unique_ptr<JobController> jobController;
        std::unique_ptr<SourceSyncDao> sourceSyncDao;
        std::unique_ptr<SourceSyncService> sourceSyncService;
        std::unique_ptr<InternalSourceSyncController>
            internalController;
        std::string internalToken;
        std::string token;
        std::string scope;
        ServerState state;
        std::thread serverThread;
    };

    TEST_F(SourceSyncE2ETest, SyncVisibilityDisappearanceAndReopen)
    {
        const std::string fresh = Timestamp("NOW()");
        const std::string oldPublication =
            Timestamp("TIMESTAMPADD(DAY, -40, NOW())");
        const std::string staleObservation =
            Timestamp("TIMESTAMPADD(DAY, -8, NOW())");

        const std::string urlA =
            "https://job-boards.greenhouse.io/gitlab/jobs/e2e-1";
        const std::string urlB =
            "https://job-boards.greenhouse.io/gitlab/jobs/e2e-2";

        const nlohmann::json jobA = MakeAdapterJob(
            token,
            "Backend Engineer",
            urlA,
            oldPublication);
        const nlohmann::json jobB = MakeAdapterJob(
            token,
            "Platform Engineer",
            urlB,
            oldPublication);

        // 1) Adapter observation is ingested through the internal API.
        const auto created = Publish(
            fresh,
            true,
            nlohmann::json::array({jobA}));

        ASSERT_TRUE(created.has_value());
        ASSERT_TRUE(created->hasJson);
        ASSERT_EQ(created->statusCode, 200);
        EXPECT_EQ(created->jsonBody["created"], 1);
        EXPECT_EQ(created->jsonBody["seen"], 1);
        EXPECT_EQ(created->jsonBody["closed"], 0);

        // 2) The job is publicly searchable and exposes freshness fields.
        const auto search =
            SendRequest("GET", SearchUrl(), "", "");

        ASSERT_TRUE(search.has_value());
        ASSERT_TRUE(search->hasJson);
        ASSERT_EQ(search->statusCode, 200);
        ASSERT_EQ(search->jsonBody["data"]["total"], 1);
        ASSERT_EQ(search->jsonBody["data"]["list"].size(), 1U);

        const auto &item =
            search->jsonBody["data"]["list"][0];

        EXPECT_EQ(item["freshness"], "fresh");
        EXPECT_EQ(item["source_status"], "ok");
        EXPECT_EQ(item["source"], "greenhouse");
        EXPECT_FALSE(item["posted_at"].is_null());
        EXPECT_FALSE(item["first_seen_at"].is_null());
        EXPECT_FALSE(item["last_seen_at"].is_null());
        EXPECT_FALSE(item["last_verified_at"].is_null());
        EXPECT_EQ(item["posted_at"], oldPublication);

        const std::uint64_t jobId =
            item["id"].get<std::uint64_t>();

        // 3) The adapter payload survives into the detail contract.
        const auto detail = SendRequest(
            "GET",
            "/api/v1/jobs/" + std::to_string(jobId),
            "",
            "");

        ASSERT_TRUE(detail.has_value());
        ASSERT_TRUE(detail->hasJson);
        ASSERT_EQ(detail->statusCode, 200);
        EXPECT_EQ(
            detail->jsonBody["data"]["company"],
            "GitLab");
        EXPECT_EQ(
            detail->jsonBody["data"]["location"],
            "Remote, France");
        EXPECT_EQ(
            detail->jsonBody["data"]["description"],
            "Build backend services.");
        EXPECT_EQ(
            detail->jsonBody["data"]["source_url"],
            urlA);
        EXPECT_EQ(
            detail->jsonBody["data"]["requirements"].size(),
            0U);

        // 4) posted_within_days filters on the source publication time.
        const auto recent = SendRequest(
            "GET",
            SearchUrl("&posted_within_days=7"),
            "",
            "");

        ASSERT_TRUE(recent.has_value());
        ASSERT_TRUE(recent->hasJson);
        EXPECT_EQ(recent->jsonBody["data"]["total"], 0);

        const auto wide = SendRequest(
            "GET",
            SearchUrl("&posted_within_days=60"),
            "",
            "");

        ASSERT_TRUE(wide.has_value());
        ASSERT_TRUE(wide->hasJson);
        EXPECT_EQ(wide->jsonBody["data"]["total"], 1);

        // 5) First complete absence: still visible, marked missing.
        const auto firstMiss = Publish(
            fresh,
            true,
            nlohmann::json::array());

        ASSERT_TRUE(firstMiss.has_value());
        ASSERT_TRUE(firstMiss->hasJson);
        EXPECT_EQ(firstMiss->jsonBody["closed"], 0);

        const auto afterFirstMiss =
            SendRequest("GET", SearchUrl(), "", "");

        ASSERT_TRUE(afterFirstMiss.has_value());
        ASSERT_TRUE(afterFirstMiss->hasJson);
        EXPECT_EQ(afterFirstMiss->jsonBody["data"]["total"], 1);
        EXPECT_EQ(
            afterFirstMiss->jsonBody["data"]["list"][0]["source_status"],
            "missing");

        // 6) Second complete absence closes it and it leaves public search.
        const auto secondMiss = Publish(
            fresh,
            true,
            nlohmann::json::array());

        ASSERT_TRUE(secondMiss.has_value());
        ASSERT_TRUE(secondMiss->hasJson);
        EXPECT_EQ(secondMiss->jsonBody["closed"], 1);

        const auto afterClose =
            SendRequest("GET", SearchUrl(), "", "");

        ASSERT_TRUE(afterClose.has_value());
        ASSERT_TRUE(afterClose->hasJson);
        EXPECT_EQ(afterClose->jsonBody["data"]["total"], 0);

        // 7) Reappearance reopens it and it becomes visible again.
        const auto reopened = Publish(
            fresh,
            true,
            nlohmann::json::array({jobA}));

        ASSERT_TRUE(reopened.has_value());
        ASSERT_TRUE(reopened->hasJson);
        EXPECT_EQ(reopened->jsonBody["created"], 0);
        EXPECT_EQ(reopened->jsonBody["updated"], 1);
        EXPECT_EQ(reopened->jsonBody["closed"], 0);

        const auto afterReopen =
            SendRequest("GET", SearchUrl(), "", "");

        ASSERT_TRUE(afterReopen.has_value());
        ASSERT_TRUE(afterReopen->hasJson);
        ASSERT_EQ(afterReopen->jsonBody["data"]["total"], 1);
        EXPECT_EQ(
            afterReopen->jsonBody["data"]["list"][0]["freshness"],
            "fresh");
        EXPECT_EQ(
            afterReopen->jsonBody["data"]["list"][0]["source_status"],
            "ok");

        // 8) A stale observation (8 days old) is hidden by default but
        //    reachable with include_stale=true.
        const auto stale = Publish(
            staleObservation,
            true,
            nlohmann::json::array({jobB}));

        ASSERT_TRUE(stale.has_value());
        ASSERT_TRUE(stale->hasJson);
        EXPECT_EQ(stale->jsonBody["created"], 1);

        const auto defaultSearch =
            SendRequest("GET", SearchUrl(), "", "");

        ASSERT_TRUE(defaultSearch.has_value());
        ASSERT_TRUE(defaultSearch->hasJson);
        EXPECT_EQ(defaultSearch->jsonBody["data"]["total"], 1);

        const auto withStale = SendRequest(
            "GET",
            SearchUrl("&include_stale=true"),
            "",
            "");

        ASSERT_TRUE(withStale.has_value());
        ASSERT_TRUE(withStale->hasJson);
        EXPECT_EQ(withStale->jsonBody["data"]["total"], 2);

        bool sawStale = false;

        for (const auto &entry :
             withStale->jsonBody["data"]["list"])
        {
            if (entry["freshness"] == "stale")
            {
                sawStale = true;
            }
        }

        EXPECT_TRUE(sawStale);
    }

} // namespace jobpilot::service
