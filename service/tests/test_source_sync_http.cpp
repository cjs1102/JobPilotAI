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
        constexpr std::uint16_t kTestPort = 19097;

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

        std::string JobUrl(
            const std::string &scope,
            const std::string &name)
        {
            return "https://example.invalid/" + scope + "/" + name;
        }

        nlohmann::json MakeJobJson(
            const std::string &source,
            const std::string &scope,
            const std::string &name)
        {
            return nlohmann::json{
                {"company", "Greenhouse Company"},
                {"title", "Backend " + name},
                {"location", "Remote"},
                {"salary_min", 20},
                {"salary_max", 40},
                {"salary_currency", "CNY"},
                {"description", "Observed " + name},
                {"source", source},
                {"source_url", JobUrl(scope, name)},
                {"external_id", "ext-" + name},
                {"published_at", "2026-09-20 08:00:00"},
                {"requirements",
                 nlohmann::json::array(
                     {{{"skill", "C++"}, {"level", 3}},
                      {{"skill", "Linux"}, {"level", 2}}})}};
        }

        std::string MakeObservationBody(
            const std::string &source,
            const std::string &scope,
            const std::string &observedAt,
            bool complete,
            const nlohmann::json &jobs)
        {
            return nlohmann::json{
                {"source", source},
                {"source_scope", scope},
                {"complete", complete},
                {"observed_at", observedAt},
                {"jobs", jobs}}
                .dump();
        }
    } // namespace

    class SourceSyncHttpTest : public ::testing::Test
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

            sourceSyncDao =
                std::make_unique<SourceSyncDao>();
            sourceSyncService =
                std::make_unique<SourceSyncService>(
                    *sourceSyncDao,
                    *pool);

            internalToken =
                jobpilot::test::UniqueJobToken(
                    "source-sync-token");
            controller =
                std::make_unique<
                    InternalSourceSyncController>(
                    *sourceSyncService,
                    internalToken);

            scope =
                jobpilot::test::UniqueJobToken(
                    "sync-http-scope");
            otherScope =
                jobpilot::test::UniqueJobToken(
                    "sync-http-other");

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
                            nullptr,
                            nullptr,
                            nullptr,
                            controller.get());

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

            RemoveScopeJobs(scope);
            RemoveScopeJobs(otherScope);
        }

        void RemoveScopeJobs(
            const std::string &scopeName)
        {
            const jobpilot::core::Result jobs =
                pool->Query(
                    "SELECT id FROM jobs WHERE source_scope = ?",
                    {scopeName});

            for (std::size_t row = 0;
                 row < jobs.rowCount();
                 ++row)
            {
                const auto &value =
                    jobs.value(row, 0);

                if (value.has_value())
                {
                    jobpilot::test::CleanupJob(
                        pool.get(),
                        static_cast<std::uint64_t>(
                            std::stoull(
                                value.value())));
                }
            }
        }

        jobpilot::core::Result ReadJob(
            const std::string &scopeName,
            const std::string &name)
        {
            return pool->Query(
                "SELECT status, source_status, missing_count, "
                "closed_at, last_seen_at, last_verified_at, "
                "source_scope, external_id, published_at "
                "FROM jobs WHERE source_url = ?",
                {JobUrl(scopeName, name)});
        }

        static std::string Text(
            const jobpilot::core::Result &result,
            std::size_t column)
        {
            return jobpilot::test::RequireValue(
                result.value(0, column));
        }

        static bool IsNull(
            const jobpilot::core::Result &result,
            std::size_t column)
        {
            return !result.value(0, column).has_value();
        }

        std::unique_ptr<jobpilot::core::ConnectionPool> pool;
        std::unique_ptr<jobpilot::core::RedisClient> redis;
        std::unique_ptr<jobpilot::core::SessionManager> sessions;
        std::unique_ptr<SessionAuthenticator> authenticator;
        std::unique_ptr<UserDao> userDao;
        std::unique_ptr<UserService> userService;
        std::unique_ptr<UserController> userController;
        std::unique_ptr<SourceSyncDao> sourceSyncDao;
        std::unique_ptr<SourceSyncService> sourceSyncService;
        std::unique_ptr<InternalSourceSyncController> controller;
        std::string internalToken;
        std::string scope;
        std::string otherScope;
        ServerState state;
        std::thread serverThread;
    };

    TEST_F(SourceSyncHttpTest, RejectsMissingOrWrongToken)
    {
        const std::string body =
            MakeObservationBody(
                "greenhouse",
                scope,
                "2026-10-01 00:00:00",
                true,
                nlohmann::json::array(
                    {MakeJobJson(
                        "greenhouse",
                        scope,
                        "token")}));

        const auto missing =
            SendRequest(
                "POST",
                "/internal/jobs/source-sync",
                body,
                "");

        ASSERT_TRUE(missing.has_value());
        ASSERT_TRUE(missing->hasJson);
        EXPECT_EQ(missing->statusCode, 401);
        EXPECT_EQ(missing->jsonBody["code"], 20001);

        const auto wrong =
            SendRequest(
                "POST",
                "/internal/jobs/source-sync",
                body,
                "wrong-token");

        ASSERT_TRUE(wrong.has_value());
        ASSERT_TRUE(wrong->hasJson);
        EXPECT_EQ(wrong->statusCode, 401);
        EXPECT_EQ(wrong->jsonBody["code"], 20001);

        const jobpilot::core::Result count =
            pool->Query(
                "SELECT COUNT(*) FROM jobs WHERE source_scope = ?",
                {scope});

        EXPECT_EQ(Text(count, 0), "0");
    }

    TEST_F(SourceSyncHttpTest, RejectsMalformedAndInvalidObservations)
    {
        const auto malformed =
            SendRequest(
                "POST",
                "/internal/jobs/source-sync",
                "{invalid",
                internalToken);

        ASSERT_TRUE(malformed.has_value());
        ASSERT_TRUE(malformed->hasJson);
        EXPECT_EQ(malformed->statusCode, 400);
        EXPECT_EQ(malformed->jsonBody["code"], 10002);

        const auto notObject =
            SendRequest(
                "POST",
                "/internal/jobs/source-sync",
                "[]",
                internalToken);

        ASSERT_TRUE(notObject.has_value());
        ASSERT_TRUE(notObject->hasJson);
        EXPECT_EQ(notObject->statusCode, 400);
        EXPECT_EQ(notObject->jsonBody["code"], 10001);

        const auto jobs =
            nlohmann::json::array(
                {MakeJobJson("greenhouse", scope, "invalid")});

        const auto missingComplete =
            SendRequest(
                "POST",
                "/internal/jobs/source-sync",
                nlohmann::json{
                    {"source", "greenhouse"},
                    {"source_scope", scope},
                    {"observed_at", "2026-10-01 00:00:00"},
                    {"jobs", jobs}}
                    .dump(),
                internalToken);

        ASSERT_TRUE(missingComplete.has_value());
        ASSERT_TRUE(missingComplete->hasJson);
        EXPECT_EQ(missingComplete->statusCode, 400);
        EXPECT_EQ(missingComplete->jsonBody["code"], 10001);

        const auto badTimestamp =
            SendRequest(
                "POST",
                "/internal/jobs/source-sync",
                MakeObservationBody(
                    "greenhouse",
                    scope,
                    "2026-10-01T00:00:00",
                    true,
                    jobs),
                internalToken);

        ASSERT_TRUE(badTimestamp.has_value());
        ASSERT_TRUE(badTimestamp->hasJson);
        EXPECT_EQ(badTimestamp->statusCode, 400);
        EXPECT_EQ(badTimestamp->jsonBody["code"], 10001);

        const auto unsupportedSource =
            SendRequest(
                "POST",
                "/internal/jobs/source-sync",
                MakeObservationBody(
                    "manual",
                    scope,
                    "2026-10-01 00:00:00",
                    true,
                    jobs),
                internalToken);

        ASSERT_TRUE(unsupportedSource.has_value());
        ASSERT_TRUE(unsupportedSource->hasJson);
        EXPECT_EQ(unsupportedSource->statusCode, 400);
        EXPECT_EQ(unsupportedSource->jsonBody["code"], 10001);

        const auto emptyIncomplete =
            SendRequest(
                "POST",
                "/internal/jobs/source-sync",
                MakeObservationBody(
                    "greenhouse",
                    scope,
                    "2026-10-01 00:00:00",
                    false,
                    nlohmann::json::array()),
                internalToken);

        ASSERT_TRUE(emptyIncomplete.has_value());
        ASSERT_TRUE(emptyIncomplete->hasJson);
        EXPECT_EQ(emptyIncomplete->statusCode, 400);
        EXPECT_EQ(emptyIncomplete->jsonBody["code"], 10001);

        const auto mismatchedSource =
            SendRequest(
                "POST",
                "/internal/jobs/source-sync",
                MakeObservationBody(
                    "greenhouse",
                    scope,
                    "2026-10-01 00:00:00",
                    true,
                    nlohmann::json::array(
                        {MakeJobJson(
                            "lever",
                            scope,
                            "invalid")})),
                internalToken);

        ASSERT_TRUE(mismatchedSource.has_value());
        ASSERT_TRUE(mismatchedSource->hasJson);
        EXPECT_EQ(mismatchedSource->statusCode, 400);
        EXPECT_EQ(mismatchedSource->jsonBody["code"], 10001);

        const auto jobsNotArray =
            SendRequest(
                "POST",
                "/internal/jobs/source-sync",
                nlohmann::json{
                    {"source", "greenhouse"},
                    {"source_scope", scope},
                    {"complete", true},
                    {"observed_at", "2026-10-01 00:00:00"},
                    {"jobs", nlohmann::json::object()}}
                    .dump(),
                internalToken);

        ASSERT_TRUE(jobsNotArray.has_value());
        ASSERT_TRUE(jobsNotArray->hasJson);
        EXPECT_EQ(jobsNotArray->statusCode, 400);
        EXPECT_EQ(jobsNotArray->jsonBody["code"], 10001);

        const jobpilot::core::Result count =
            pool->Query(
                "SELECT COUNT(*) FROM jobs WHERE source_scope = ?",
                {scope});

        EXPECT_EQ(Text(count, 0), "0");
    }

    TEST_F(SourceSyncHttpTest, AppliesCompleteAndIncompleteObservations)
    {
        const auto created =
            SendRequest(
                "POST",
                "/internal/jobs/source-sync",
                MakeObservationBody(
                    "greenhouse",
                    scope,
                    "2026-10-01 00:00:00",
                    true,
                    nlohmann::json::array(
                        {MakeJobJson("greenhouse", scope, "one"),
                         MakeJobJson("greenhouse", scope, "two")})),
                internalToken);

        ASSERT_TRUE(created.has_value());
        ASSERT_TRUE(created->hasJson);
        EXPECT_EQ(created->statusCode, 200);
        EXPECT_EQ(created->jsonBody["created"], 2);
        EXPECT_EQ(created->jsonBody["updated"], 0);
        EXPECT_EQ(created->jsonBody["seen"], 2);
        EXPECT_EQ(created->jsonBody["closed"], 0);
        EXPECT_EQ(created->jsonBody["complete"], true);

        const jobpilot::core::Result one = ReadJob(scope, "one");

        ASSERT_EQ(one.rowCount(), 1U);
        EXPECT_EQ(Text(one, 0), "1");
        EXPECT_EQ(Text(one, 1), "ok");
        EXPECT_EQ(Text(one, 2), "0");
        EXPECT_TRUE(IsNull(one, 3));
        EXPECT_EQ(Text(one, 4), "2026-10-01 00:00:00");
        EXPECT_EQ(Text(one, 5), "2026-10-01 00:00:00");
        EXPECT_EQ(Text(one, 6), scope);
        EXPECT_EQ(Text(one, 7), "ext-one");
        EXPECT_EQ(Text(one, 8), "2026-09-20 08:00:00");

        const auto firstMiss =
            SendRequest(
                "POST",
                "/internal/jobs/source-sync",
                MakeObservationBody(
                    "greenhouse",
                    scope,
                    "2026-10-01 01:00:00",
                    true,
                    nlohmann::json::array(
                        {MakeJobJson("greenhouse", scope, "one")})),
                internalToken);

        ASSERT_TRUE(firstMiss.has_value());
        ASSERT_TRUE(firstMiss->hasJson);
        EXPECT_EQ(firstMiss->statusCode, 200);
        EXPECT_EQ(firstMiss->jsonBody["seen"], 1);
        EXPECT_EQ(firstMiss->jsonBody["closed"], 0);

        const jobpilot::core::Result missed = ReadJob(scope, "two");

        ASSERT_EQ(missed.rowCount(), 1U);
        EXPECT_EQ(Text(missed, 0), "1");
        EXPECT_EQ(Text(missed, 1), "missing");
        EXPECT_EQ(Text(missed, 2), "1");
        EXPECT_TRUE(IsNull(missed, 3));

        const auto incomplete =
            SendRequest(
                "POST",
                "/internal/jobs/source-sync",
                MakeObservationBody(
                    "greenhouse",
                    scope,
                    "2026-10-01 02:00:00",
                    false,
                    nlohmann::json::array(
                        {MakeJobJson("greenhouse", scope, "one")})),
                internalToken);

        ASSERT_TRUE(incomplete.has_value());
        ASSERT_TRUE(incomplete->hasJson);
        EXPECT_EQ(incomplete->statusCode, 200);
        EXPECT_EQ(incomplete->jsonBody["closed"], 0);
        EXPECT_EQ(incomplete->jsonBody["complete"], false);

        const jobpilot::core::Result afterIncomplete =
            ReadJob(scope, "two");

        ASSERT_EQ(afterIncomplete.rowCount(), 1U);
        EXPECT_EQ(Text(afterIncomplete, 0), "1");
        EXPECT_EQ(Text(afterIncomplete, 1), "missing");
        EXPECT_EQ(Text(afterIncomplete, 2), "1");

        const auto secondMiss =
            SendRequest(
                "POST",
                "/internal/jobs/source-sync",
                MakeObservationBody(
                    "greenhouse",
                    scope,
                    "2026-10-01 03:00:00",
                    true,
                    nlohmann::json::array(
                        {MakeJobJson("greenhouse", scope, "one")})),
                internalToken);

        ASSERT_TRUE(secondMiss.has_value());
        ASSERT_TRUE(secondMiss->hasJson);
        EXPECT_EQ(secondMiss->statusCode, 200);
        EXPECT_EQ(secondMiss->jsonBody["closed"], 1);

        const jobpilot::core::Result closed = ReadJob(scope, "two");

        ASSERT_EQ(closed.rowCount(), 1U);
        EXPECT_EQ(Text(closed, 0), "0");
        EXPECT_EQ(Text(closed, 1), "closed");
        EXPECT_EQ(Text(closed, 2), "2");
        EXPECT_EQ(Text(closed, 3), "2026-10-01 03:00:00");

        const auto reappeared =
            SendRequest(
                "POST",
                "/internal/jobs/source-sync",
                MakeObservationBody(
                    "greenhouse",
                    scope,
                    "2026-10-01 04:00:00",
                    true,
                    nlohmann::json::array(
                        {MakeJobJson("greenhouse", scope, "one"),
                         MakeJobJson("greenhouse", scope, "two")})),
                internalToken);

        ASSERT_TRUE(reappeared.has_value());
        ASSERT_TRUE(reappeared->hasJson);
        EXPECT_EQ(reappeared->statusCode, 200);
        EXPECT_EQ(reappeared->jsonBody["created"], 0);
        EXPECT_EQ(reappeared->jsonBody["updated"], 2);
        EXPECT_EQ(reappeared->jsonBody["closed"], 0);

        const jobpilot::core::Result reopened = ReadJob(scope, "two");

        ASSERT_EQ(reopened.rowCount(), 1U);
        EXPECT_EQ(Text(reopened, 0), "1");
        EXPECT_EQ(Text(reopened, 1), "ok");
        EXPECT_EQ(Text(reopened, 2), "0");
        EXPECT_TRUE(IsNull(reopened, 3));
        EXPECT_EQ(Text(reopened, 4), "2026-10-01 04:00:00");
    }

    TEST_F(SourceSyncHttpTest, ClosesAfterGraceWindowWhenBoardIsEmpty)
    {
        const auto created =
            SendRequest(
                "POST",
                "/internal/jobs/source-sync",
                MakeObservationBody(
                    "lever",
                    scope,
                    "2026-10-01 00:00:00",
                    true,
                    nlohmann::json::array(
                        {MakeJobJson("lever", scope, "grace")})),
                internalToken);

        ASSERT_TRUE(created.has_value());
        ASSERT_TRUE(created->hasJson);
        EXPECT_EQ(created->statusCode, 200);
        EXPECT_EQ(created->jsonBody["created"], 1);

        const auto grace =
            SendRequest(
                "POST",
                "/internal/jobs/source-sync",
                MakeObservationBody(
                    "lever",
                    scope,
                    "2026-10-03 01:00:00",
                    true,
                    nlohmann::json::array()),
                internalToken);

        ASSERT_TRUE(grace.has_value());
        ASSERT_TRUE(grace->hasJson);
        EXPECT_EQ(grace->statusCode, 200);
        EXPECT_EQ(grace->jsonBody["seen"], 0);
        EXPECT_EQ(grace->jsonBody["closed"], 1);

        const jobpilot::core::Result closed = ReadJob(scope, "grace");

        ASSERT_EQ(closed.rowCount(), 1U);
        EXPECT_EQ(Text(closed, 0), "0");
        EXPECT_EQ(Text(closed, 1), "closed");
        EXPECT_EQ(Text(closed, 2), "1");
        EXPECT_EQ(Text(closed, 3), "2026-10-03 01:00:00");
    }

    TEST_F(SourceSyncHttpTest, KeepsOtherScopesUntouched)
    {
        const auto createdOther =
            SendRequest(
                "POST",
                "/internal/jobs/source-sync",
                MakeObservationBody(
                    "greenhouse",
                    otherScope,
                    "2026-10-01 00:00:00",
                    true,
                    nlohmann::json::array(
                        {MakeJobJson(
                            "greenhouse",
                            otherScope,
                            "other")})),
                internalToken);

        ASSERT_TRUE(createdOther.has_value());
        ASSERT_TRUE(createdOther->hasJson);
        ASSERT_EQ(createdOther->statusCode, 200);

        const auto firstMiss =
            SendRequest(
                "POST",
                "/internal/jobs/source-sync",
                MakeObservationBody(
                    "greenhouse",
                    scope,
                    "2026-10-01 01:00:00",
                    true,
                    nlohmann::json::array()),
                internalToken);

        ASSERT_TRUE(firstMiss.has_value());
        ASSERT_TRUE(firstMiss->hasJson);
        EXPECT_EQ(firstMiss->statusCode, 200);
        EXPECT_EQ(firstMiss->jsonBody["closed"], 0);

        const jobpilot::core::Result other =
            ReadJob(otherScope, "other");

        ASSERT_EQ(other.rowCount(), 1U);
        EXPECT_EQ(Text(other, 0), "1");
        EXPECT_EQ(Text(other, 1), "ok");
        EXPECT_EQ(Text(other, 2), "0");
        EXPECT_TRUE(IsNull(other, 3));
    }

    TEST_F(SourceSyncHttpTest, ParsesAndRejectsThe0014ExtraFields)
    {
        nlohmann::json validJob =
            MakeJobJson("greenhouse", scope, "extra-valid");
        validJob["education_required"] = 3;
        validJob["experience_years_min"] = 0;
        validJob["experience_years_max"] = 3;
        validJob["job_nature"] = 1;
        validJob["deadline"] = "2026-10-31 04:00:00";
        validJob["deadline_date_only"] = true;
        validJob["apply_url"] =
            "https://example.invalid/apply/extra-valid";

        const auto accepted =
            SendRequest(
                "POST",
                "/internal/jobs/source-sync",
                MakeObservationBody(
                    "greenhouse",
                    scope,
                    "2026-10-01 00:00:00",
                    false,
                    nlohmann::json::array({validJob})),
                internalToken);

        ASSERT_TRUE(accepted.has_value());
        ASSERT_TRUE(accepted->hasJson);
        EXPECT_EQ(accepted->statusCode, 200);
        EXPECT_EQ(accepted->jsonBody["created"], 1);

        const jobpilot::core::Result stored =
            pool->Query(
                "SELECT education_required, experience_years_min, "
                "experience_years_max, job_nature, deadline, "
                "deadline_date_only, apply_url "
                "FROM jobs WHERE source_url = ?",
                {JobUrl(scope, "extra-valid")});

        ASSERT_EQ(stored.rowCount(), 1U);
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 0)),
            "3");
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 1)),
            "0");
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 2)),
            "3");
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 3)),
            "1");
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 4)),
            "2026-10-31 04:00:00");
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 5)),
            "1");
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 6)),
            "https://example.invalid/apply/extra-valid");

        auto statusFor =
            [this](nlohmann::json job)
        {
            const auto response =
                SendRequest(
                    "POST",
                    "/internal/jobs/source-sync",
                    MakeObservationBody(
                        "greenhouse",
                        scope,
                        "2026-10-01 00:00:00",
                        false,
                        nlohmann::json::array({job})),
                    internalToken);

            return response.has_value()
                       ? response->statusCode
                       : 0;
        };

        nlohmann::json educationOutOfRange =
            MakeJobJson("greenhouse", scope, "extra-education");
        educationOutOfRange["education_required"] = 6;
        EXPECT_EQ(statusFor(educationOutOfRange), 400);

        nlohmann::json natureOutOfRange =
            MakeJobJson("greenhouse", scope, "extra-nature");
        natureOutOfRange["job_nature"] = 4;
        EXPECT_EQ(statusFor(natureOutOfRange), 400);

        nlohmann::json invertedYears =
            MakeJobJson("greenhouse", scope, "extra-years");
        invertedYears["experience_years_min"] = 5;
        invertedYears["experience_years_max"] = 3;
        EXPECT_EQ(statusFor(invertedYears), 400);

        nlohmann::json malformedDeadline =
            MakeJobJson("greenhouse", scope, "extra-deadline");
        malformedDeadline["deadline"] = "2026-10-31";
        EXPECT_EQ(statusFor(malformedDeadline), 400);

        nlohmann::json dateOnlyWithInstant =
            MakeJobJson("greenhouse", scope, "extra-kind");
        dateOnlyWithInstant["deadline"] = "2026-10-31 10:30:00";
        dateOnlyWithInstant["deadline_date_only"] = true;
        EXPECT_EQ(statusFor(dateOnlyWithInstant), 400);

        nlohmann::json flagWithoutDeadline =
            MakeJobJson("greenhouse", scope, "extra-flag");
        flagWithoutDeadline["deadline_date_only"] = false;
        EXPECT_EQ(statusFor(flagWithoutDeadline), 400);

        nlohmann::json applyRelative =
            MakeJobJson("greenhouse", scope, "extra-apply-relative");
        applyRelative["apply_url"] = "/relative/apply";
        EXPECT_EQ(statusFor(applyRelative), 400);

        nlohmann::json applyJavascript =
            MakeJobJson("greenhouse", scope, "extra-apply-javascript");
        applyJavascript["apply_url"] = "javascript:alert(1)";
        EXPECT_EQ(statusFor(applyJavascript), 400);

        nlohmann::json applyTooLong =
            MakeJobJson("greenhouse", scope, "extra-apply-too-long");
        applyTooLong["apply_url"] =
            "https://e.co/" + std::string(500, 'x');
        EXPECT_EQ(statusFor(applyTooLong), 400);
    }

    TEST_F(SourceSyncHttpTest, KeepsObservedDeadlineWhenANewFetchOmitsIt)
    {
        nlohmann::json first =
            MakeJobJson("greenhouse", scope, "coalesce");
        first["deadline"] = "2026-10-31 04:00:00";
        first["deadline_date_only"] = true;
        first["job_nature"] = 1;

        const auto created =
            SendRequest(
                "POST",
                "/internal/jobs/source-sync",
                MakeObservationBody(
                    "greenhouse",
                    scope,
                    "2026-10-01 00:00:00",
                    false,
                    nlohmann::json::array({first})),
                internalToken);

        ASSERT_TRUE(created.has_value());
        ASSERT_EQ(created->statusCode, 200);
        EXPECT_EQ(created->jsonBody["created"], 1);

        // A later fetch that cannot see those facts must not erase them.
        nlohmann::json second =
            MakeJobJson("greenhouse", scope, "coalesce");
        second["title"] = "Backend coalesce v2";

        const auto updated =
            SendRequest(
                "POST",
                "/internal/jobs/source-sync",
                MakeObservationBody(
                    "greenhouse",
                    scope,
                    "2026-10-02 00:00:00",
                    false,
                    nlohmann::json::array({second})),
                internalToken);

        ASSERT_TRUE(updated.has_value());
        ASSERT_EQ(updated->statusCode, 200);
        EXPECT_EQ(updated->jsonBody["updated"], 1);

        const jobpilot::core::Result stored =
            pool->Query(
                "SELECT deadline, deadline_date_only, job_nature "
                "FROM jobs WHERE source_url = ?",
                {JobUrl(scope, "coalesce")});

        ASSERT_EQ(stored.rowCount(), 1U);
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 0)),
            "2026-10-31 04:00:00");
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 1)),
            "1");
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 2)),
            "1");
    }

    TEST_F(SourceSyncHttpTest, ManualRecordWinsOverObservation)
    {
        const std::string manualUrl = JobUrl(scope, "manual-owner");

        const std::uint64_t manualJobId =
            jobpilot::test::InsertJob(
                pool.get(),
                "Manual Company",
                "Manual Job",
                std::string("Beijing"),
                std::string("Manual description"),
                manualUrl,
                1,
                "");

        pool->Execute(
            "UPDATE jobs SET education_required = 5, job_nature = 2, "
            "deadline = '2026-12-01 04:00:00', deadline_date_only = 1 "
            "WHERE id = ?",
            {manualJobId});

        jobpilot::test::InsertJobRequirement(
            pool.get(),
            manualJobId,
            "ManualSkill",
            3);

        nlohmann::json observed =
            MakeJobJson("greenhouse", scope, "manual-owner");
        observed["education_required"] = 1;
        observed["job_nature"] = 3;
        observed["deadline"] = "2026-11-15 10:30:00";
        observed["deadline_date_only"] = false;
        observed["requirements"] =
            nlohmann::json::array(
                {{{"skill", "ObservedSkill"}, {"level", 1}}});

        const auto response =
            SendRequest(
                "POST",
                "/internal/jobs/source-sync",
                MakeObservationBody(
                    "greenhouse",
                    scope,
                    "2026-10-01 00:00:00",
                    true,
                    nlohmann::json::array({observed})),
                internalToken);

        ASSERT_TRUE(response.has_value());
        ASSERT_TRUE(response->hasJson);
        ASSERT_EQ(response->statusCode, 200);
        EXPECT_EQ(response->jsonBody["created"], 0);
        EXPECT_EQ(response->jsonBody["updated"], 0);
        EXPECT_EQ(response->jsonBody["seen"], 1);
        EXPECT_EQ(response->jsonBody["skipped_manual"], 1);
        ASSERT_EQ(
            response->jsonBody["skipped_manual_urls"].size(),
            1U);
        EXPECT_EQ(
            response->jsonBody["skipped_manual_urls"][0],
            manualUrl);

        const jobpilot::core::Result stored =
            pool->Query(
                "SELECT company, title, description, source, "
                "source_scope, education_required, job_nature, "
                "deadline, deadline_date_only, status, "
                "missing_count, source_status, closed_at, last_seen_at "
                "FROM jobs WHERE id = ?",
                {manualJobId});

        ASSERT_EQ(stored.rowCount(), 1U);
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 0)),
            "Manual Company");
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 1)),
            "Manual Job");
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 2)),
            "Manual description");
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 3)),
            "manual");
        EXPECT_FALSE(stored.value(0, 4).has_value());
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 5)),
            "5");
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 6)),
            "2");
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 7)),
            "2026-12-01 04:00:00");
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 8)),
            "1");
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 9)),
            "1");
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 10)),
            "0");
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 11)),
            "unknown");
        EXPECT_FALSE(stored.value(0, 12).has_value());
        // The observation must not have refreshed the manual row's freshness.
        EXPECT_FALSE(stored.value(0, 13).has_value());

        const jobpilot::core::Result requirements =
            pool->Query(
                "SELECT skill FROM job_requirements "
                "WHERE job_id = ? ORDER BY skill",
                {manualJobId});

        ASSERT_EQ(requirements.rowCount(), 1U);
        EXPECT_EQ(
            jobpilot::test::RequireValue(
                requirements.value(0, 0)),
            "ManualSkill");

        jobpilot::test::CleanupJob(pool.get(), manualJobId);
    }

} // namespace jobpilot::service
