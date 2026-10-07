#include <arpa/inet.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
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
#include "jobpilot/service/job/job_controller.h"
#include "jobpilot/service/job/job_dao.h"
#include "jobpilot/service/job/job_ranking_service.h"
#include "jobpilot/service/job/job_service.h"
#include "jobpilot/service/router.h"
#include "jobpilot/service/user/user_controller.h"
#include "jobpilot/service/user/user_dao.h"
#include "jobpilot/service/user/user_service.h"
#include "test_job_support.h"
#include "test_user_support.h"

namespace jobpilot::service
{
    namespace
    {
        constexpr std::uint16_t kTestPort = 19094;

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

                sent += static_cast<std::size_t>(result);
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
            timeout.tv_sec = 3;

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
            const std::string &path,
            const std::string &authorization = "")
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
                "GET " + path +
                " HTTP/1.1\r\n"
                "Host: 127.0.0.1\r\n"
                "Connection: close\r\n";

            if (!authorization.empty())
            {
                request +=
                    "Authorization: " +
                    authorization + "\r\n";
            }

            request += "\r\n";

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
    } // namespace

    class JobHttpTest : public ::testing::Test
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
            userDao =
                std::make_unique<UserDao>(*pool);
            userService =
                std::make_unique<UserService>(
                    *userDao,
                    *sessions);
            userController =
                std::make_unique<UserController>(
                    *userService);
            authenticator =
                std::make_unique<SessionAuthenticator>(
                    *sessions);
            jobDao =
                std::make_unique<JobDao>(*pool);
            jobService =
                std::make_unique<JobService>(*jobDao);
            rankingService =
                std::make_unique<JobRankingService>(
                    *jobDao,
                    *userDao,
                    *authenticator);
            jobController =
                std::make_unique<JobController>(
                    *jobService,
                    rankingService.get());

            searchToken =
                jobpilot::test::UniqueJobToken(
                    "job-http");
            location =
                jobpilot::test::UniqueJobToken(
                    "Location");

            olderId =
                jobpilot::test::InsertJob(
                    pool.get(),
                    "Acme",
                    searchToken + " older",
                    location,
                    "Old description",
                    jobpilot::test::UniqueJobToken(
                        "job-http-url"),
                    1,
                    "2026-10-01 10:00:00");
            newerId =
                jobpilot::test::InsertJob(
                    pool.get(),
                    "Beta",
                    searchToken + " C++",
                    location,
                    "New description",
                    jobpilot::test::UniqueJobToken(
                        "job-http-url"),
                    1,
                    "2026-10-02 10:00:00");
            inactiveId =
                jobpilot::test::InsertJob(
                    pool.get(),
                    "Hidden",
                    searchToken + " inactive",
                    location,
                    std::nullopt,
                    jobpilot::test::UniqueJobToken(
                        "job-http-url"),
                    0,
                    "2026-10-03 10:00:00");

            pool->Execute(
                "UPDATE jobs SET salary_min = ?, salary_max = ? "
                "WHERE id = ?",
                {std::uint64_t(50),
                 std::uint64_t(70),
                 olderId});
            pool->Execute(
                "UPDATE jobs SET salary_min = ?, salary_max = ? "
                "WHERE id = ?",
                {std::uint64_t(20),
                 std::uint64_t(35),
                 newerId});

            jobpilot::test::InsertJobRequirement(
                pool.get(),
                newerId,
                "Redis",
                2);
            jobpilot::test::InsertJobRequirement(
                pool.get(),
                newerId,
                "C++",
                3);

            email =
                jobpilot::test::UniqueEmail(
                    "job-http-user");
            const std::optional<std::uint64_t> createdUser =
                userDao->CreateUser(
                    "job-http-user",
                    email,
                    "hash");
            ASSERT_TRUE(createdUser.has_value());
            userId = createdUser.value();
            token =
                jobpilot::test::UniqueJobToken(
                    "job-http-token");
            sessions->Set(token, userId);

            UserProfile profile;
            profile.userId = userId;
            profile.targetPosition = "C++";
            profile.location = location;
            profile.salaryMin = 20;
            profile.salaryMax = 35;
            ASSERT_TRUE(
                userDao->UpsertProfile(profile));
            ASSERT_TRUE(
                userDao->UpsertSkill(
                    userId,
                    "C++",
                    3)
                    .has_value());
            ASSERT_TRUE(
                userDao->UpsertSkill(
                    userId,
                    "Redis",
                    2)
                    .has_value());

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
                            jobController.get());

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

            jobpilot::test::CleanupJob(
                pool.get(),
                olderId);
            jobpilot::test::CleanupJob(
                pool.get(),
                newerId);
            jobpilot::test::CleanupJob(
                pool.get(),
                inactiveId);
            sessions->Remove(token);
            jobpilot::test::CleanupUser(
                pool.get(),
                email);
        }

        ServerState state;
        std::thread serverThread;
        std::unique_ptr<
            jobpilot::core::ConnectionPool>
            pool;
        std::unique_ptr<jobpilot::core::RedisClient> redis;
        std::unique_ptr<jobpilot::core::SessionManager>
            sessions;
        std::unique_ptr<UserDao> userDao;
        std::unique_ptr<UserService> userService;
        std::unique_ptr<UserController> userController;
        std::unique_ptr<SessionAuthenticator>
            authenticator;
        std::unique_ptr<JobDao> jobDao;
        std::unique_ptr<JobService> jobService;
        std::unique_ptr<JobRankingService>
            rankingService;
        std::unique_ptr<JobController>
            jobController;
        std::string searchToken;
        std::string location;
        std::uint64_t olderId = 0;
        std::uint64_t newerId = 0;
        std::uint64_t inactiveId = 0;
        std::string email;
        std::string token;
        std::uint64_t userId = 0;
    };

    TEST_F(JobHttpTest, SearchesWithPaginationAndPercentDecoding)
    {
        const std::optional<HttpResponseData> firstPage =
            SendRequest(
                "/api/v1/jobs?keyword=" +
                searchToken +
                "%20C%2B%2B&location=" +
                location +
                "&page=1&page_size=1");

        ASSERT_TRUE(firstPage.has_value());
        ASSERT_TRUE(firstPage->hasJson);
        EXPECT_EQ(firstPage->statusCode, 200);
        EXPECT_EQ(
            firstPage->jsonBody["data"]["total"],
            1);
        ASSERT_EQ(
            firstPage->jsonBody["data"]["list"].size(),
            1U);
        EXPECT_EQ(
            firstPage->jsonBody["data"]["list"][0]["id"],
            newerId);

        const std::optional<HttpResponseData> empty =
            SendRequest(
                "/api/v1/jobs?keyword=" +
                jobpilot::test::UniqueJobToken(
                    "missing-job"));

        ASSERT_TRUE(empty.has_value());
        ASSERT_TRUE(empty->hasJson);
        EXPECT_EQ(empty->statusCode, 200);
        EXPECT_TRUE(
            empty->jsonBody["data"]["list"].empty());
    }

    TEST_F(JobHttpTest, ReturnsDetailAndNotFound)
    {
        const std::optional<HttpResponseData> detail =
            SendRequest(
                "/api/v1/jobs/" +
                std::to_string(newerId));

        ASSERT_TRUE(detail.has_value());
        ASSERT_TRUE(detail->hasJson);
        EXPECT_EQ(detail->statusCode, 200);
        EXPECT_EQ(
            detail->jsonBody["data"]["id"],
            newerId);
        ASSERT_EQ(
            detail->jsonBody["data"]["requirements"].size(),
            2U);
        EXPECT_EQ(
            detail->jsonBody["data"]["requirements"][0]["skill"],
            "C++");
        EXPECT_EQ(
            detail->jsonBody["data"]["requirements"][0]["level"],
            3);

        const std::optional<HttpResponseData> inactive =
            SendRequest(
                "/api/v1/jobs/" +
                std::to_string(inactiveId));

        ASSERT_TRUE(inactive.has_value());
        ASSERT_TRUE(inactive->hasJson);
        EXPECT_EQ(inactive->statusCode, 404);
        EXPECT_EQ(
            inactive->jsonBody["code"],
            40001);

        const std::optional<HttpResponseData> missing =
            SendRequest(
                "/api/v1/jobs/999999999");

        ASSERT_TRUE(missing.has_value());
        ASSERT_TRUE(missing->hasJson);
        EXPECT_EQ(missing->statusCode, 404);
        EXPECT_EQ(
            missing->jsonBody["code"],
            40001);
    }

    TEST_F(JobHttpTest, ReturnsEmptyPageWithRealTotal)
    {
        const std::optional<HttpResponseData> beyond =
            SendRequest(
                "/api/v1/jobs?keyword=" +
                searchToken +
                "&location=" +
                location +
                "&page=1000&page_size=1");

        ASSERT_TRUE(beyond.has_value());
        ASSERT_TRUE(beyond->hasJson);
        EXPECT_EQ(beyond->statusCode, 200);
        EXPECT_TRUE(
            beyond->jsonBody["data"]["list"].empty());
        EXPECT_EQ(
            beyond->jsonBody["data"]["total"],
            2);
    }

    TEST_F(JobHttpTest, EnforcesKeywordLengthBoundary)
    {
        const std::string keyword128(128, 'a');
        const std::string keyword129(129, 'a');

        ASSERT_EQ(keyword128.size(), 128U);
        ASSERT_EQ(keyword129.size(), 129U);

        const std::optional<HttpResponseData> accepted =
            SendRequest(
                "/api/v1/jobs?keyword=" +
                keyword128);

        ASSERT_TRUE(accepted.has_value());
        ASSERT_TRUE(accepted->hasJson);
        EXPECT_EQ(accepted->statusCode, 200);

        const std::optional<HttpResponseData> rejected =
            SendRequest(
                "/api/v1/jobs?keyword=" +
                keyword129);

        ASSERT_TRUE(rejected.has_value());
        ASSERT_TRUE(rejected->hasJson);
        EXPECT_EQ(rejected->statusCode, 400);
        EXPECT_EQ(
            rejected->jsonBody["code"],
            10001);
    }

    TEST_F(JobHttpTest, RejectsMalformedQueryAndPath)
    {
        const std::optional<HttpResponseData> invalidPage =
            SendRequest(
                "/api/v1/jobs?page=0");

        ASSERT_TRUE(invalidPage.has_value());
        ASSERT_TRUE(invalidPage->hasJson);
        EXPECT_EQ(invalidPage->statusCode, 400);
        EXPECT_EQ(
            invalidPage->jsonBody["code"],
            10001);

        const std::optional<HttpResponseData> duplicate =
            SendRequest(
                "/api/v1/jobs?keyword=a&keyword=b");

        ASSERT_TRUE(duplicate.has_value());
        EXPECT_EQ(duplicate->statusCode, 400);

        const std::optional<HttpResponseData> malformedPercent =
            SendRequest(
                "/api/v1/jobs?keyword=%ZZ");

        ASSERT_TRUE(malformedPercent.has_value());
        EXPECT_EQ(
            malformedPercent->statusCode,
            400);

        const std::optional<HttpResponseData> invalidId =
            SendRequest(
                "/api/v1/jobs/not-a-number");

        ASSERT_TRUE(invalidId.has_value());
        ASSERT_TRUE(invalidId->hasJson);
        EXPECT_EQ(invalidId->statusCode, 400);
        EXPECT_EQ(
            invalidId->jsonBody["code"],
            10001);
    }

    TEST_F(JobHttpTest, FiltersBySalaryAndSkill)
    {
        const std::optional<HttpResponseData> response =
            SendRequest(
                "/api/v1/jobs?keyword=" +
                searchToken +
                "&skill=C%2B%2B&salary_min=25&salary_max=30");

        ASSERT_TRUE(response.has_value());
        ASSERT_TRUE(response->hasJson);
        EXPECT_EQ(response->statusCode, 200);
        ASSERT_EQ(
            response->jsonBody["data"]["list"].size(),
            1U);
        EXPECT_EQ(
            response->jsonBody["data"]["list"][0]["id"],
            newerId);
    }

    TEST_F(JobHttpTest, ReturnsDeterministicRecommendations)
    {
        const std::optional<HttpResponseData> unauthenticated =
            SendRequest(
                "/api/v1/jobs/recommended?keyword=" +
                searchToken);

        ASSERT_TRUE(unauthenticated.has_value());
        ASSERT_TRUE(unauthenticated->hasJson);
        EXPECT_EQ(unauthenticated->statusCode, 401);
        EXPECT_EQ(
            unauthenticated->jsonBody["code"],
            20001);

        const std::optional<HttpResponseData> invalidToken =
            SendRequest(
                "/api/v1/jobs/recommended?keyword=" +
                    searchToken,
                "Bearer invalid-token");

        ASSERT_TRUE(invalidToken.has_value());
        ASSERT_TRUE(invalidToken->hasJson);
        EXPECT_EQ(invalidToken->statusCode, 401);
        EXPECT_EQ(
            invalidToken->jsonBody["code"],
            20001);

        const std::optional<HttpResponseData> response =
            SendRequest(
                "/api/v1/jobs/recommended?keyword=" +
                    searchToken +
                    "&page=1&page_size=10",
                "Bearer " + token);

        ASSERT_TRUE(response.has_value());
        ASSERT_TRUE(response->hasJson);
        EXPECT_EQ(response->statusCode, 200);
        ASSERT_EQ(
            response->jsonBody["data"]["list"].size(),
            2U);
        EXPECT_EQ(
            response->jsonBody["data"]["list"][0]["id"],
            newerId);
        EXPECT_EQ(
            response->jsonBody["data"]["list"][0]
                             ["score_version"],
            "rule_v1");
        EXPECT_TRUE(
            response->jsonBody["data"]["list"][0]
                             ["score"]
                .is_number());
        EXPECT_TRUE(
            response->jsonBody["data"]["list"][0]
                             ["score_breakdown"]
                .is_object());
    }

    TEST_F(JobHttpTest, AppliesFreshnessVisibilityAndResponseFields)
    {
        const std::string token =
            jobpilot::test::UniqueJobToken(
                "job-http-freshness");

        const std::uint64_t freshId =
            jobpilot::test::InsertJob(
                pool.get(),
                "Fresh Co",
                token + " fresh",
                std::nullopt,
                std::nullopt,
                jobpilot::test::UniqueJobToken(
                    "job-http-url"),
                1,
                "2026-10-01 10:00:00");
        const std::uint64_t staleId =
            jobpilot::test::InsertJob(
                pool.get(),
                "Stale Co",
                token + " stale",
                std::nullopt,
                std::nullopt,
                jobpilot::test::UniqueJobToken(
                    "job-http-url"),
                1,
                "2026-10-01 10:00:00");
        const std::uint64_t closedId =
            jobpilot::test::InsertJob(
                pool.get(),
                "Closed Co",
                token + " closed",
                std::nullopt,
                std::nullopt,
                jobpilot::test::UniqueJobToken(
                    "job-http-url"),
                1,
                "2026-10-01 10:00:00");

        pool->Execute(
            "UPDATE jobs SET "
            "last_seen_at = TIMESTAMPADD(HOUR, -2, NOW()), "
            "last_verified_at = TIMESTAMPADD(HOUR, -2, NOW()), "
            "published_at = TIMESTAMPADD(DAY, -1, NOW()), "
            "source_status = 'ok' WHERE id = ?",
            {freshId});
        pool->Execute(
            "UPDATE jobs SET "
            "last_seen_at = TIMESTAMPADD(DAY, -9, NOW()), "
            "last_verified_at = TIMESTAMPADD(DAY, -9, NOW()), "
            "published_at = TIMESTAMPADD(DAY, -40, NOW()), "
            "source_status = 'ok' WHERE id = ?",
            {staleId});
        pool->Execute(
            "UPDATE jobs SET closed_at = NOW(), "
            "source_status = 'closed' WHERE id = ?",
            {closedId});

        const std::optional<HttpResponseData> defaultSearch =
            SendRequest(
                "/api/v1/jobs?keyword=" + token);

        ASSERT_TRUE(defaultSearch.has_value());
        ASSERT_TRUE(defaultSearch->hasJson);
        EXPECT_EQ(defaultSearch->statusCode, 200);
        EXPECT_EQ(
            defaultSearch->jsonBody["data"]["total"],
            1);
        ASSERT_EQ(
            defaultSearch->jsonBody["data"]["list"].size(),
            1U);

        const auto &item =
            defaultSearch->jsonBody["data"]["list"][0];

        EXPECT_EQ(item["id"], freshId);
        EXPECT_EQ(item["freshness"], "fresh");
        EXPECT_EQ(item["source_status"], "ok");
        EXPECT_FALSE(item["posted_at"].is_null());
        EXPECT_FALSE(item["first_seen_at"].is_null());
        EXPECT_FALSE(item["last_seen_at"].is_null());
        EXPECT_FALSE(item["last_verified_at"].is_null());

        const std::optional<HttpResponseData> withStale =
            SendRequest(
                "/api/v1/jobs?keyword=" + token +
                "&include_stale=true");

        ASSERT_TRUE(withStale.has_value());
        ASSERT_TRUE(withStale->hasJson);
        EXPECT_EQ(withStale->statusCode, 200);
        EXPECT_EQ(
            withStale->jsonBody["data"]["total"],
            2);

        const std::optional<HttpResponseData> posted =
            SendRequest(
                "/api/v1/jobs?keyword=" + token +
                "&include_stale=true&posted_within_days=7");

        ASSERT_TRUE(posted.has_value());
        ASSERT_TRUE(posted->hasJson);
        EXPECT_EQ(posted->statusCode, 200);
        EXPECT_EQ(
            posted->jsonBody["data"]["total"],
            1);
        EXPECT_EQ(
            posted->jsonBody["data"]["list"][0]["id"],
            freshId);

        const std::optional<HttpResponseData> invalidDays =
            SendRequest(
                "/api/v1/jobs?keyword=" + token +
                "&posted_within_days=0");

        ASSERT_TRUE(invalidDays.has_value());
        ASSERT_TRUE(invalidDays->hasJson);
        EXPECT_EQ(invalidDays->statusCode, 400);
        EXPECT_EQ(invalidDays->jsonBody["code"], 10001);

        const std::optional<HttpResponseData> invalidStale =
            SendRequest(
                "/api/v1/jobs?keyword=" + token +
                "&include_stale=yes");

        ASSERT_TRUE(invalidStale.has_value());
        ASSERT_TRUE(invalidStale->hasJson);
        EXPECT_EQ(invalidStale->statusCode, 400);
        EXPECT_EQ(invalidStale->jsonBody["code"], 10001);

        jobpilot::test::CleanupJob(pool.get(), freshId);
        jobpilot::test::CleanupJob(pool.get(), staleId);
        jobpilot::test::CleanupJob(pool.get(), closedId);
    }
} // namespace jobpilot::service
