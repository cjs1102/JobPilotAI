#include <arpa/inet.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
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
#include "jobpilot/service/ai/ai_controller.h"
#include "jobpilot/service/ai/ai_internal_controller.h"
#include "jobpilot/service/ai/ai_task_dao.h"
#include "jobpilot/service/ai/ai_task_service.h"
#include "jobpilot/service/auth/session_authenticator.h"
#include "jobpilot/service/job/job_dao.h"
#include "jobpilot/service/resume/resume_dao.h"
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
        constexpr std::uint16_t kTestPort = 19099;

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
            const std::string &body = std::string(),
            const std::string &authorization = std::string(),
            const std::string &internalToken = std::string())
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

            if (!authorization.empty())
            {
                request +=
                    "Authorization: " +
                    authorization + "\r\n";
            }

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
                    static_cast<std::size_t>(received));
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

    class AiHttpTest : public ::testing::Test
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
            userDao = std::make_unique<UserDao>(*pool);
            userService =
                std::make_unique<UserService>(
                    *userDao,
                    *sessions);
            userController =
                std::make_unique<UserController>(
                    *userService);

            jobDao = std::make_unique<JobDao>(*pool);
            resumeDao = std::make_unique<ResumeDao>(*pool);

            aiTaskDao = std::make_unique<AiTaskDao>(*pool);
            aiTaskService =
                std::make_unique<AiTaskService>(
                    *aiTaskDao,
                    *jobDao,
                    *resumeDao,
                    *userDao,
                    AiTaskConfig{});

            internalToken =
                jobpilot::test::UniqueJobToken(
                    "ai-http-internal");

            aiController =
                std::make_unique<AiController>(
                    *aiTaskService,
                    *authenticator);
            aiInternalController =
                std::make_unique<AiInternalController>(
                    *aiTaskService,
                    internalToken);

            location =
                jobpilot::test::UniqueJobToken("Shenzhen");
            jobId = jobpilot::test::InsertJob(
                pool.get(),
                "Example",
                "C++ Backend Engineer",
                location,
                "Build backend services",
                jobpilot::test::UniqueJobToken("ai-http-url"),
                1,
                "2026-10-01 10:00:00");

            jobpilot::test::InsertJobRequirement(
                pool.get(),
                jobId,
                "C++",
                3);
            jobpilot::test::InsertJobRequirement(
                pool.get(),
                jobId,
                "Redis",
                2);

            pool->Execute(
                "UPDATE jobs SET salary_min = ?, salary_max = ? "
                "WHERE id = ?",
                {std::uint64_t(20),
                 std::uint64_t(35),
                 jobId});

            email =
                jobpilot::test::UniqueEmail("ai-http-user");
            otherEmail =
                jobpilot::test::UniqueEmail("ai-http-other");

            const std::optional<std::uint64_t> createdUser =
                userDao->CreateUser(
                    "ai-http-user",
                    email,
                    "hash");
            ASSERT_TRUE(createdUser.has_value());
            userId = createdUser.value();

            const std::optional<std::uint64_t> createdOther =
                userDao->CreateUser(
                    "ai-http-other",
                    otherEmail,
                    "hash");
            ASSERT_TRUE(createdOther.has_value());
            otherUserId = createdOther.value();

            token =
                jobpilot::test::UniqueJobToken("ai-http-token");
            otherToken =
                jobpilot::test::UniqueJobToken("ai-http-other-token");
            sessions->Set(token, userId);
            sessions->Set(otherToken, otherUserId);

            UserProfile profile;
            profile.userId = userId;
            profile.targetPosition = "C++ Backend";
            profile.location = location;
            profile.salaryMin = 20;
            profile.salaryMax = 30;
            profile.summary = "Backend engineer";
            ASSERT_TRUE(userDao->UpsertProfile(profile));
            ASSERT_TRUE(
                userDao->UpsertSkill(userId, "C++", 3)
                    .has_value());
            ASSERT_TRUE(
                userDao->UpsertSkill(userId, "Redis", 2)
                    .has_value());

            jobpilot::core::ConnectionPool::Transaction transaction =
                pool->BeginTransaction();

            const std::optional<std::uint64_t> createdResume =
                resumeDao->CreateResume(
                    transaction,
                    userId,
                    "Backend resume");
            ASSERT_TRUE(createdResume.has_value());
            resumeId = createdResume.value();

            const std::optional<std::uint64_t> createdVersion =
                resumeDao->CreateVersion(
                    transaction,
                    resumeId,
                    1,
                    std::nullopt,
                    std::string("C++ Redis Linux"));
            ASSERT_TRUE(createdVersion.has_value());

            const std::optional<std::uint64_t> otherResume =
                resumeDao->CreateResume(
                    transaction,
                    otherUserId,
                    "Other resume");
            ASSERT_TRUE(otherResume.has_value());
            otherResumeId = otherResume.value();

            ASSERT_TRUE(
                resumeDao
                    ->CreateVersion(
                        transaction,
                        otherResumeId,
                        1,
                        std::nullopt,
                        std::string("Go only"))
                    .has_value());

            transaction.Commit();

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
                            nullptr,
                            aiController.get(),
                            aiInternalController.get());

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
                std::rethrow_exception(state.exception);
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

            pool->Execute(
                "DELETE FROM ai_tasks WHERE user_id IN (?, ?)",
                {userId, otherUserId});
            sessions->Remove(token);
            sessions->Remove(otherToken);
            jobpilot::test::CleanupJob(pool.get(), jobId);
            jobpilot::test::CleanupUser(pool.get(), email);
            jobpilot::test::CleanupUser(
                pool.get(),
                otherEmail);
        }

        std::string MatchBody() const
        {
            return nlohmann::json{
                {"job_id", jobId},
                {"resume_id", resumeId}}
                .dump();
        }

        std::uint64_t InsertPendingTask() const
        {
            const jobpilot::core::ExecutionResult created =
                pool->Execute(
                    "INSERT INTO ai_tasks "
                    "(user_id, task_type, job_id, resume_id, status, "
                    "result, rule_result, error_msg, retry_count) "
                    "VALUES (?, 'job_match', ?, ?, 0, NULL, ?, NULL, 0)",
                    {userId,
                     jobId,
                     resumeId,
                     std::string(
                         "{\"score_version\": \"match_rule_v1\", "
                         "\"score\": 82.0, \"source\": \"rule\"}")});

            return created.lastInsertId;
        }

        std::int64_t LeaseSeconds()
        {
            const jobpilot::core::Result result =
                pool->Query(
                    "SELECT TIMESTAMPDIFF(SECOND, NOW(), "
                    "lease_expire_at) FROM ai_tasks "
                    "WHERE id = (SELECT MAX(id) FROM ai_tasks "
                    "WHERE user_id = ?)",
                    {userId});

            if (result.rowCount() == 0 ||
                !result.value(0, 0).has_value())
            {
                return -1;
            }

            return std::stol(result.value(0, 0).value());
        }

        ServerState state;
        std::thread serverThread;
        std::unique_ptr<jobpilot::core::ConnectionPool> pool;
        std::unique_ptr<jobpilot::core::RedisClient> redis;
        std::unique_ptr<jobpilot::core::SessionManager> sessions;
        std::unique_ptr<SessionAuthenticator> authenticator;
        std::unique_ptr<UserDao> userDao;
        std::unique_ptr<UserService> userService;
        std::unique_ptr<UserController> userController;
        std::unique_ptr<JobDao> jobDao;
        std::unique_ptr<ResumeDao> resumeDao;
        std::unique_ptr<AiTaskDao> aiTaskDao;
        std::unique_ptr<AiTaskService> aiTaskService;
        std::unique_ptr<AiController> aiController;
        std::unique_ptr<AiInternalController> aiInternalController;
        std::uint64_t jobId = 0;
        std::uint64_t resumeId = 0;
        std::uint64_t otherResumeId = 0;
        std::uint64_t userId = 0;
        std::uint64_t otherUserId = 0;
        std::string email;
        std::string otherEmail;
        std::string token;
        std::string otherToken;
        std::string internalToken;
        std::string location;
    };

    TEST_F(AiHttpTest, PublicEndpointsRequireBearerAuthentication)
    {
        const auto create =
            SendRequest(
                "POST",
                "/api/v1/ai/match",
                MatchBody());

        ASSERT_TRUE(create.has_value());
        ASSERT_TRUE(create->hasJson);
        EXPECT_EQ(create->statusCode, 401);
        EXPECT_EQ(create->jsonBody["code"], 20001);

        const auto invalid =
            SendRequest(
                "POST",
                "/api/v1/ai/match",
                MatchBody(),
                "Bearer invalid-token");

        ASSERT_TRUE(invalid.has_value());
        ASSERT_TRUE(invalid->hasJson);
        EXPECT_EQ(invalid->statusCode, 401);
        EXPECT_EQ(invalid->jsonBody["code"], 20001);

        const auto poll =
            SendRequest(
                "GET",
                "/api/v1/ai/tasks/1");

        ASSERT_TRUE(poll.has_value());
        ASSERT_TRUE(poll->hasJson);
        EXPECT_EQ(poll->statusCode, 401);
        EXPECT_EQ(poll->jsonBody["code"], 20001);
    }

    TEST_F(AiHttpTest, RejectsInvalidMatchRequests)
    {
        const auto malformed =
            SendRequest(
                "POST",
                "/api/v1/ai/match",
                "{invalid",
                "Bearer " + token);

        ASSERT_TRUE(malformed.has_value());
        ASSERT_TRUE(malformed->hasJson);
        EXPECT_EQ(malformed->statusCode, 400);
        EXPECT_EQ(malformed->jsonBody["code"], 10002);

        const auto missingResume =
            SendRequest(
                "POST",
                "/api/v1/ai/match",
                nlohmann::json{{"job_id", jobId}}.dump(),
                "Bearer " + token);

        ASSERT_TRUE(missingResume.has_value());
        ASSERT_TRUE(missingResume->hasJson);
        EXPECT_EQ(missingResume->statusCode, 400);
        EXPECT_EQ(missingResume->jsonBody["code"], 10001);

        // Missing / inactive job -> 40001 with the 404 mapping.
        const auto unknownJob =
            SendRequest(
                "POST",
                "/api/v1/ai/match",
                nlohmann::json{
                    {"job_id", 999999999},
                    {"resume_id", resumeId}}
                    .dump(),
                "Bearer " + token);

        ASSERT_TRUE(unknownJob.has_value());
        ASSERT_TRUE(unknownJob->hasJson);
        EXPECT_EQ(unknownJob->statusCode, 404);
        EXPECT_EQ(unknownJob->jsonBody["code"], 40001);

        // A resume owned by somebody else -> 40002.
        const auto foreignResume =
            SendRequest(
                "POST",
                "/api/v1/ai/match",
                nlohmann::json{
                    {"job_id", jobId},
                    {"resume_id", otherResumeId}}
                    .dump(),
                "Bearer " + token);

        ASSERT_TRUE(foreignResume.has_value());
        ASSERT_TRUE(foreignResume->hasJson);
        EXPECT_EQ(foreignResume->statusCode, 404);
        EXPECT_EQ(foreignResume->jsonBody["code"], 40002);
    }

    TEST_F(AiHttpTest, CreatesResolvedTaskAndIsIdempotent)
    {
        const auto created =
            SendRequest(
                "POST",
                "/api/v1/ai/match",
                MatchBody(),
                "Bearer " + token);

        ASSERT_TRUE(created.has_value());
        ASSERT_TRUE(created->hasJson);
        EXPECT_EQ(created->statusCode, 200);
        EXPECT_EQ(created->jsonBody["code"], 0);
        ASSERT_TRUE(
            created->jsonBody["data"]["task_id"].is_number());
        const std::uint64_t taskId =
            created->jsonBody["data"]["task_id"]
                .get<std::uint64_t>();
        EXPECT_GT(taskId, 0U);

        const auto repeated =
            SendRequest(
                "POST",
                "/api/v1/ai/match",
                MatchBody(),
                "Bearer " + token);

        ASSERT_TRUE(repeated.has_value());
        ASSERT_TRUE(repeated->hasJson);
        EXPECT_EQ(repeated->statusCode, 200);
        // Only a non-terminal task is reused (ExecPlan 0013 section 5.2), so
        // a second request after the first resolved immediately is a new
        // task. The reuse path itself is covered by ReusesTheOpenTaskOverHttp.
        EXPECT_NE(
            repeated->jsonBody["data"]["task_id"]
                .get<std::uint64_t>(),
            taskId);

        const auto taskCount =
            pool->Query(
                "SELECT COUNT(*) FROM ai_tasks "
                "WHERE user_id = ? AND job_id = ? AND resume_id = ?",
                {userId, jobId, resumeId});

        ASSERT_EQ(taskCount.rowCount(), 1U);
        EXPECT_EQ(taskCount.value(0, 0).value(), "2");

        const auto polled =
            SendRequest(
                "GET",
                "/api/v1/ai/tasks/" +
                    std::to_string(taskId),
                "",
                "Bearer " + token);

        ASSERT_TRUE(polled.has_value());
        ASSERT_TRUE(polled->hasJson);
        EXPECT_EQ(polled->statusCode, 200);
        EXPECT_EQ(polled->jsonBody["code"], 0);

        const auto &data = polled->jsonBody["data"];

        EXPECT_EQ(
            data["task_id"].get<std::uint64_t>(),
            taskId);
        EXPECT_EQ(data["task_type"], "job_match");
        // Enhancement disabled: the task is born successful with the rule
        // baseline as its final result (ExecPlan 0013 section 6.3).
        EXPECT_EQ(data["status"], 2);
        EXPECT_TRUE(data["error_msg"].is_null());
        ASSERT_TRUE(data["rule_result"].is_object());
        ASSERT_TRUE(data["result"].is_object());
        EXPECT_EQ(data["result"], data["rule_result"]);
        EXPECT_EQ(
            data["rule_result"]["score_version"],
            "match_rule_v1");
        EXPECT_EQ(
            data["rule_result"]["source"],
            "rule");
        EXPECT_FALSE(data["created_at"].is_null());
        EXPECT_FALSE(data["updated_at"].is_null());
    }

    TEST_F(AiHttpTest, ReusesTheOpenTaskOverHttp)
    {
        const std::uint64_t pendingId = InsertPendingTask();

        const auto reused =
            SendRequest(
                "POST",
                "/api/v1/ai/match",
                MatchBody(),
                "Bearer " + token);

        ASSERT_TRUE(reused.has_value());
        ASSERT_TRUE(reused->hasJson);
        EXPECT_EQ(reused->statusCode, 200);
        EXPECT_EQ(reused->jsonBody["code"], 0);
        EXPECT_EQ(
            reused->jsonBody["data"]["task_id"]
                .get<std::uint64_t>(),
            pendingId);

        const jobpilot::core::Result count =
            pool->Query(
                "SELECT COUNT(*) FROM ai_tasks "
                "WHERE user_id = ? AND job_id = ? AND resume_id = ?",
                {userId, jobId, resumeId});

        ASSERT_EQ(count.rowCount(), 1U);
        EXPECT_EQ(count.value(0, 0).value(), "1");

        // The pre-existing pending task and its rule baseline are returned
        // untouched: still pending and still awaiting a worker.
        const auto polled =
            SendRequest(
                "GET",
                "/api/v1/ai/tasks/" +
                    std::to_string(pendingId),
                "",
                "Bearer " + token);

        ASSERT_TRUE(polled.has_value());
        ASSERT_TRUE(polled->hasJson);
        EXPECT_EQ(polled->statusCode, 200);
        EXPECT_EQ(polled->jsonBody["data"]["status"], 0);
        EXPECT_TRUE(polled->jsonBody["data"]["result"].is_null());
        ASSERT_TRUE(
            polled->jsonBody["data"]["rule_result"].is_object());
        EXPECT_EQ(
            polled->jsonBody["data"]["rule_result"]["score"],
            82.0);
        EXPECT_EQ(
            polled->jsonBody["data"]["rule_result"]
                             ["score_version"],
            "match_rule_v1");
    }

    TEST_F(AiHttpTest, HidesForeignTasksBehindTheSameNotFoundCode)
    {
        const auto created =
            SendRequest(
                "POST",
                "/api/v1/ai/match",
                MatchBody(),
                "Bearer " + token);

        ASSERT_TRUE(created.has_value());
        ASSERT_TRUE(created->hasJson);
        ASSERT_EQ(created->statusCode, 200);
        const std::uint64_t taskId =
            created->jsonBody["data"]["task_id"]
                .get<std::uint64_t>();

        const auto foreign =
            SendRequest(
                "GET",
                "/api/v1/ai/tasks/" +
                    std::to_string(taskId),
                "",
                "Bearer " + otherToken);

        ASSERT_TRUE(foreign.has_value());
        ASSERT_TRUE(foreign->hasJson);
        EXPECT_EQ(foreign->statusCode, 404);
        EXPECT_EQ(foreign->jsonBody["code"], 50001);

        const auto missing =
            SendRequest(
                "GET",
                "/api/v1/ai/tasks/999999999",
                "",
                "Bearer " + token);

        ASSERT_TRUE(missing.has_value());
        ASSERT_TRUE(missing->hasJson);
        EXPECT_EQ(missing->statusCode, 404);
        EXPECT_EQ(missing->jsonBody["code"], 50001);
        EXPECT_EQ(
            missing->jsonBody["message"],
            "ai task not found");

        const auto notANumber =
            SendRequest(
                "GET",
                "/api/v1/ai/tasks/not-a-number",
                "",
                "Bearer " + token);

        ASSERT_TRUE(notANumber.has_value());
        ASSERT_TRUE(notANumber->hasJson);
        EXPECT_EQ(notANumber->statusCode, 400);
        EXPECT_EQ(notANumber->jsonBody["code"], 10001);
    }

    TEST_F(AiHttpTest, RejectsMissingOrWrongInternalToken)
    {
        const auto missing =
            SendRequest(
                "POST",
                "/internal/ai/tasks/claim",
                R"({"worker_id": "worker-a"})");

        ASSERT_TRUE(missing.has_value());
        ASSERT_TRUE(missing->hasJson);
        EXPECT_EQ(missing->statusCode, 401);
        EXPECT_EQ(missing->jsonBody["code"], 20001);

        const auto wrong =
            SendRequest(
                "POST",
                "/internal/ai/tasks/claim",
                R"({"worker_id": "worker-a"})",
                "",
                "wrong-token");

        ASSERT_TRUE(wrong.has_value());
        ASSERT_TRUE(wrong->hasJson);
        EXPECT_EQ(wrong->statusCode, 401);
        EXPECT_EQ(wrong->jsonBody["code"], 20001);

        const auto inputMissing =
            SendRequest(
                "GET",
                "/internal/ai/tasks/1/input?worker_id=worker-a");

        ASSERT_TRUE(inputMissing.has_value());
        ASSERT_TRUE(inputMissing->hasJson);
        EXPECT_EQ(inputMissing->statusCode, 401);
        EXPECT_EQ(inputMissing->jsonBody["code"], 20001);
    }

    TEST_F(AiHttpTest, EmptyQueueClaimReturnsNull)
    {
        const auto claimed =
            SendRequest(
                "POST",
                "/internal/ai/tasks/claim",
                R"({"worker_id": "worker-empty"})",
                "",
                internalToken);

        ASSERT_TRUE(claimed.has_value());
        ASSERT_TRUE(claimed->hasJson);
        EXPECT_EQ(claimed->statusCode, 200);
        EXPECT_TRUE(claimed->jsonBody["task"].is_null());
        EXPECT_EQ(claimed->jsonBody["requeued_stale"], 0);
        EXPECT_EQ(claimed->jsonBody["expired_failed"], 0);
    }

    TEST_F(AiHttpTest, RunsTheWorkerLifecycleOverHttp)
    {
        const std::uint64_t taskId = InsertPendingTask();

        const auto claimed =
            SendRequest(
                "POST",
                "/internal/ai/tasks/claim",
                R"({"worker_id": "worker-a"})",
                "",
                internalToken);

        ASSERT_TRUE(claimed.has_value());
        ASSERT_TRUE(claimed->hasJson);
        EXPECT_EQ(claimed->statusCode, 200);
        ASSERT_TRUE(claimed->jsonBody["task"].is_object());
        EXPECT_EQ(
            claimed->jsonBody["task"]["id"].get<std::uint64_t>(),
            taskId);
        EXPECT_EQ(
            claimed->jsonBody["task"]["task_type"],
            "job_match");
        EXPECT_EQ(
            claimed->jsonBody["task"]["job_id"].get<std::uint64_t>(),
            jobId);
        EXPECT_EQ(
            claimed->jsonBody["task"]["resume_id"]
                .get<std::uint64_t>(),
            resumeId);
        EXPECT_EQ(claimed->jsonBody["task"]["retry_count"], 1);

        const auto input =
            SendRequest(
                "GET",
                "/internal/ai/tasks/" +
                    std::to_string(taskId) +
                    "/input?worker_id=worker-a",
                "",
                "",
                internalToken);

        ASSERT_TRUE(input.has_value());
        ASSERT_TRUE(input->hasJson);
        EXPECT_EQ(input->statusCode, 200);

        const auto &bundle = input->jsonBody;

        EXPECT_EQ(
            bundle["task"]["id"].get<std::uint64_t>(),
            taskId);
        EXPECT_EQ(bundle["task"]["task_type"], "job_match");
        EXPECT_EQ(bundle["task"]["retry_count"], 1);
        EXPECT_EQ(
            bundle["job"]["id"].get<std::uint64_t>(),
            jobId);
        EXPECT_EQ(bundle["job"]["company"], "Example");
        EXPECT_EQ(
            bundle["job"]["title"],
            "C++ Backend Engineer");
        EXPECT_EQ(bundle["job"]["salary_min"], 20);
        EXPECT_EQ(bundle["job"]["salary_max"], 35);
        EXPECT_EQ(bundle["job"]["salary_currency"], "CNY");
        EXPECT_EQ(
            bundle["job"]["description"],
            "Build backend services");
        ASSERT_EQ(bundle["job"]["requirements"].size(), 2U);
        EXPECT_EQ(
            bundle["job"]["requirements"][0]["skill"],
            "C++");
        EXPECT_EQ(
            bundle["job"]["requirements"][0]["level"],
            3);
        ASSERT_TRUE(bundle["profile"].is_object());
        EXPECT_EQ(
            bundle["profile"]["target_position"],
            "C++ Backend");
        ASSERT_EQ(bundle["skills"].size(), 2U);
        EXPECT_EQ(bundle["resume"]["resume_id"], resumeId);
        EXPECT_EQ(bundle["resume"]["version"], 1);
        EXPECT_EQ(
            bundle["resume"]["content"],
            "C++ Redis Linux");
        ASSERT_TRUE(bundle["rule_result"].is_object());
        EXPECT_EQ(
            bundle["rule_result"]["score_version"],
            "match_rule_v1");

        // Complete without the internal token: 401.
        const auto wrongComplete =
            SendRequest(
                "POST",
                "/internal/ai/tasks/" +
                    std::to_string(taskId) +
                    "/complete",
                R"({"worker_id": "worker-b", "result": {"score": 90}})");

        ASSERT_TRUE(wrongComplete.has_value());
        ASSERT_TRUE(wrongComplete->hasJson);
        EXPECT_EQ(wrongComplete->statusCode, 401);

        const auto wrongWorkerComplete =
            SendRequest(
                "POST",
                "/internal/ai/tasks/" +
                    std::to_string(taskId) +
                    "/complete",
                R"({"worker_id": "worker-b", "result": {"score": 90}})",
                "",
                internalToken);

        ASSERT_TRUE(wrongWorkerComplete.has_value());
        ASSERT_TRUE(wrongWorkerComplete->hasJson);
        EXPECT_EQ(wrongWorkerComplete->statusCode, 404);
        EXPECT_EQ(wrongWorkerComplete->jsonBody["code"], 50001);

        const auto wrongWorkerFail =
            SendRequest(
                "POST",
                "/internal/ai/tasks/" +
                    std::to_string(taskId) +
                    "/fail",
                R"({"worker_id": "worker-b", )"
                R"("error_msg": "nope", "retryable": true})",
                "",
                internalToken);

        ASSERT_TRUE(wrongWorkerFail.has_value());
        ASSERT_TRUE(wrongWorkerFail->hasJson);
        EXPECT_EQ(wrongWorkerFail->statusCode, 404);
        EXPECT_EQ(wrongWorkerFail->jsonBody["code"], 50001);

        const auto completed =
            SendRequest(
                "POST",
                "/internal/ai/tasks/" +
                    std::to_string(taskId) +
                    "/complete",
                R"({"worker_id": "worker-a", "result": {"score": 82.0, )"
                R"("score_version": "match_rule_v1", )"
                R"("ai": {"degraded": true}}})",
                "",
                internalToken);

        ASSERT_TRUE(completed.has_value());
        ASSERT_TRUE(completed->hasJson);
        EXPECT_EQ(completed->statusCode, 200);
        EXPECT_EQ(completed->jsonBody["status"], 2);

        // The merged result is visible through the public poll while the
        // deterministic baseline survives untouched.
        const auto polled =
            SendRequest(
                "GET",
                "/api/v1/ai/tasks/" +
                    std::to_string(taskId),
                "",
                "Bearer " + token);

        ASSERT_TRUE(polled.has_value());
        ASSERT_TRUE(polled->hasJson);
        EXPECT_EQ(polled->statusCode, 200);

        const auto &data = polled->jsonBody["data"];

        EXPECT_EQ(data["status"], 2);
        EXPECT_EQ(
            data["result"]["ai"]["degraded"],
            true);
        EXPECT_EQ(
            data["rule_result"]["score_version"],
            "match_rule_v1");
        EXPECT_EQ(
            data["rule_result"]["source"],
            "rule");
    }

    TEST_F(AiHttpTest, RetryableFailRequeuesAndPermanentFailTerminates)
    {
        const std::uint64_t taskId = InsertPendingTask();

        const auto claimed =
            SendRequest(
                "POST",
                "/internal/ai/tasks/claim",
                R"({"worker_id": "worker-a"})",
                "",
                internalToken);

        ASSERT_TRUE(claimed.has_value());
        ASSERT_TRUE(claimed->hasJson);
        ASSERT_EQ(claimed->statusCode, 200);
        ASSERT_TRUE(claimed->jsonBody["task"].is_object());

        const auto requeued =
            SendRequest(
                "POST",
                "/internal/ai/tasks/" +
                    std::to_string(taskId) +
                    "/fail",
                R"({"worker_id": "worker-a", )"
                R"("error_msg": "provider timeout", "retryable": true})",
                "",
                internalToken);

        ASSERT_TRUE(requeued.has_value());
        ASSERT_TRUE(requeued->hasJson);
        EXPECT_EQ(requeued->statusCode, 200);
        EXPECT_EQ(requeued->jsonBody["status"], 0);

        const auto reclaimed =
            SendRequest(
                "POST",
                "/internal/ai/tasks/claim",
                R"({"worker_id": "worker-a"})",
                "",
                internalToken);

        ASSERT_TRUE(reclaimed.has_value());
        ASSERT_TRUE(reclaimed->hasJson);
        ASSERT_TRUE(reclaimed->jsonBody["task"].is_object());
        EXPECT_EQ(
            reclaimed->jsonBody["task"]["id"].get<std::uint64_t>(),
            taskId);
        EXPECT_EQ(reclaimed->jsonBody["task"]["retry_count"], 2);

        const auto terminated =
            SendRequest(
                "POST",
                "/internal/ai/tasks/" +
                    std::to_string(taskId) +
                    "/fail",
                R"({"worker_id": "worker-a", )"
                R"("error_msg": "invalid provider response", )"
                R"("retryable": false})",
                "",
                internalToken);

        ASSERT_TRUE(terminated.has_value());
        ASSERT_TRUE(terminated->hasJson);
        EXPECT_EQ(terminated->statusCode, 200);
        EXPECT_EQ(terminated->jsonBody["status"], 3);

        const auto polled =
            SendRequest(
                "GET",
                "/api/v1/ai/tasks/" +
                    std::to_string(taskId),
                "",
                "Bearer " + token);

        ASSERT_TRUE(polled.has_value());
        ASSERT_TRUE(polled->hasJson);
        EXPECT_EQ(polled->statusCode, 200);
        EXPECT_EQ(polled->jsonBody["data"]["status"], 3);
        EXPECT_EQ(
            polled->jsonBody["data"]["error_msg"],
            "invalid provider response");
        ASSERT_TRUE(
            polled->jsonBody["data"]["rule_result"].is_object());
        EXPECT_TRUE(
            polled->jsonBody["data"]["result"].is_null());
    }

    TEST_F(AiHttpTest, RefusesInputWithoutALiveLease)
    {
        const std::uint64_t taskId = InsertPendingTask();

        const auto claimed =
            SendRequest(
                "POST",
                "/internal/ai/tasks/claim",
                R"({"worker_id": "worker-a"})",
                "",
                internalToken);

        ASSERT_TRUE(claimed.has_value());
        ASSERT_TRUE(claimed->hasJson);
        ASSERT_EQ(claimed->statusCode, 200);
        ASSERT_TRUE(claimed->jsonBody["task"].is_object());

        const auto wrongWorker =
            SendRequest(
                "GET",
                "/internal/ai/tasks/" +
                    std::to_string(taskId) +
                    "/input?worker_id=worker-b",
                "",
                "",
                internalToken);

        ASSERT_TRUE(wrongWorker.has_value());
        ASSERT_TRUE(wrongWorker->hasJson);
        EXPECT_EQ(wrongWorker->statusCode, 404);
        EXPECT_EQ(wrongWorker->jsonBody["code"], 50001);

        const auto unclaimed =
            SendRequest(
                "GET",
                "/internal/ai/tasks/" +
                    std::to_string(taskId + 100000) +
                    "/input?worker_id=worker-a",
                "",
                "",
                internalToken);

        ASSERT_TRUE(unclaimed.has_value());
        ASSERT_TRUE(unclaimed->hasJson);
        EXPECT_EQ(unclaimed->statusCode, 404);
        EXPECT_EQ(unclaimed->jsonBody["code"], 50001);

        const auto missingWorker =
            SendRequest(
                "GET",
                "/internal/ai/tasks/" +
                    std::to_string(taskId) +
                    "/input",
                "",
                "",
                internalToken);

        ASSERT_TRUE(missingWorker.has_value());
        ASSERT_TRUE(missingWorker->hasJson);
        EXPECT_EQ(missingWorker->statusCode, 400);
        EXPECT_EQ(missingWorker->jsonBody["code"], 10001);
    }

    TEST_F(AiHttpTest, ClampsRequestedLeaseSeconds)
    {
        InsertPendingTask();

        const auto capped =
            SendRequest(
                "POST",
                "/internal/ai/tasks/claim",
                R"({"worker_id": "worker-clamp", "lease_seconds": 1000000})",
                "",
                internalToken);

        ASSERT_TRUE(capped.has_value());
        ASSERT_TRUE(capped->hasJson);
        EXPECT_EQ(capped->statusCode, 200);
        ASSERT_TRUE(capped->jsonBody["task"].is_object());

        const std::int64_t cappedSeconds = LeaseSeconds();

        EXPECT_GE(cappedSeconds, 3595);
        EXPECT_LE(cappedSeconds, 3601);

        // A requested 0 is clamped up to the 1 second minimum.
        InsertPendingTask();

        const auto minimal =
            SendRequest(
                "POST",
                "/internal/ai/tasks/claim",
                R"({"worker_id": "worker-clamp-min", "lease_seconds": 0})",
                "",
                internalToken);

        ASSERT_TRUE(minimal.has_value());
        ASSERT_TRUE(minimal->hasJson);
        EXPECT_EQ(minimal->statusCode, 200);
        ASSERT_TRUE(minimal->jsonBody["task"].is_object());

        const std::int64_t minimalSeconds = LeaseSeconds();

        EXPECT_GE(minimalSeconds, 0);
        EXPECT_LE(minimalSeconds, 2);
    }

    TEST_F(AiHttpTest, RejectsMalformedInternalBodies)
    {
        const auto malformed =
            SendRequest(
                "POST",
                "/internal/ai/tasks/claim",
                "{invalid",
                "",
                internalToken);

        ASSERT_TRUE(malformed.has_value());
        ASSERT_TRUE(malformed->hasJson);
        EXPECT_EQ(malformed->statusCode, 400);
        EXPECT_EQ(malformed->jsonBody["code"], 10002);

        const auto notObject =
            SendRequest(
                "POST",
                "/internal/ai/tasks/claim",
                "[]",
                "",
                internalToken);

        ASSERT_TRUE(notObject.has_value());
        ASSERT_TRUE(notObject->hasJson);
        EXPECT_EQ(notObject->statusCode, 400);
        EXPECT_EQ(notObject->jsonBody["code"], 10001);

        const auto noWorker =
            SendRequest(
                "POST",
                "/internal/ai/tasks/claim",
                R"({"lease_seconds": 30})",
                "",
                internalToken);

        ASSERT_TRUE(noWorker.has_value());
        ASSERT_TRUE(noWorker->hasJson);
        EXPECT_EQ(noWorker->statusCode, 400);
        EXPECT_EQ(noWorker->jsonBody["code"], 10001);

        const auto badLease =
            SendRequest(
                "POST",
                "/internal/ai/tasks/claim",
                R"({"worker_id": "worker-a", "lease_seconds": "long"})",
                "",
                internalToken);

        ASSERT_TRUE(badLease.has_value());
        ASSERT_TRUE(badLease->hasJson);
        EXPECT_EQ(badLease->statusCode, 400);
        EXPECT_EQ(badLease->jsonBody["code"], 10001);

        const std::uint64_t taskId = InsertPendingTask();

        const auto missingResult =
            SendRequest(
                "POST",
                "/internal/ai/tasks/" +
                    std::to_string(taskId) +
                    "/complete",
                R"({"worker_id": "worker-a"})",
                "",
                internalToken);

        ASSERT_TRUE(missingResult.has_value());
        ASSERT_TRUE(missingResult->hasJson);
        EXPECT_EQ(missingResult->statusCode, 400);
        EXPECT_EQ(missingResult->jsonBody["code"], 10001);

        const auto resultNotObject =
            SendRequest(
                "POST",
                "/internal/ai/tasks/" +
                    std::to_string(taskId) +
                    "/complete",
                R"({"worker_id": "worker-a", "result": "plain"})",
                "",
                internalToken);

        ASSERT_TRUE(resultNotObject.has_value());
        ASSERT_TRUE(resultNotObject->hasJson);
        EXPECT_EQ(resultNotObject->statusCode, 400);
        EXPECT_EQ(resultNotObject->jsonBody["code"], 10001);

        const auto badRetryable =
            SendRequest(
                "POST",
                "/internal/ai/tasks/" +
                    std::to_string(taskId) +
                    "/fail",
                R"({"worker_id": "worker-a", )"
                R"("error_msg": "boom", "retryable": "yes"})",
                "",
                internalToken);

        ASSERT_TRUE(badRetryable.has_value());
        ASSERT_TRUE(badRetryable->hasJson);
        EXPECT_EQ(badRetryable->statusCode, 400);
        EXPECT_EQ(badRetryable->jsonBody["code"], 10001);
    }

} // namespace jobpilot::service
