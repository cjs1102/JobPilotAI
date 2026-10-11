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
#include "jobpilot/service/application/application_controller.h"
#include "jobpilot/service/application/application_dao.h"
#include "jobpilot/service/application/application_service.h"
#include "jobpilot/service/auth/session_authenticator.h"
#include "jobpilot/service/job/job_dao.h"
#include "jobpilot/service/resume/resume_dao.h"
#include "jobpilot/service/router.h"
#include "jobpilot/service/user/user_controller.h"
#include "jobpilot/service/user/user_dao.h"
#include "jobpilot/service/user/user_service.h"
#include "test_job_support.h"
#include "test_resume_support.h"
#include "test_user_support.h"

namespace jobpilot::service
{
    namespace
    {
        constexpr std::uint16_t kTestPort = 19095;

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
            const std::string &method,
            const std::string &path,
            const std::string &body = "",
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
                std::stoi(rawResponse.substr(9, 3));
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

    class ApplicationHttpTest : public ::testing::Test
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
            jobDao =
                std::make_unique<JobDao>(*pool);
            resumeDao =
                std::make_unique<ResumeDao>(*pool);
            applicationDao =
                std::make_unique<ApplicationDao>(*pool);
            applicationService =
                std::make_unique<ApplicationService>(
                    *applicationDao,
                    *jobDao,
                    *resumeDao,
                    *pool,
                    *authenticator);
            applicationController =
                std::make_unique<ApplicationController>(
                    *applicationService);

            const std::string email =
                TrackEmail("application-http");
            const std::optional<std::uint64_t> created =
                userDao->CreateUser(
                    "http-user",
                    email,
                    "hash");
            ASSERT_TRUE(created.has_value());
            userId = created.value();

            token = jobpilot::test::UniqueJobToken(
                "application-token");
            tokens.push_back(token);
            sessions->Set(token, userId);

            jobId =
                jobpilot::test::InsertJob(
                    pool.get(),
                    "Acme",
                    "Application Job",
                    "Shenzhen",
                    "description",
                    jobpilot::test::UniqueJobToken(
                        "application-http-job"),
                    1,
                    "2026-10-01 10:00:00");

            const jobpilot::test::ResumeVersionFixture resume =
                jobpilot::test::InsertResumeVersion(
                    pool.get(),
                    userId,
                    "Application Resume",
                    "content");
            resumeVersionId = resume.versionId;

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
                            applicationController.get());

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

            for (const std::string &sessionToken : tokens)
            {
                redis->Del("session:" + sessionToken);
            }

            for (const std::string &email : emails)
            {
                jobpilot::test::CleanupUser(
                    pool.get(),
                    email);
            }

            if (jobId != 0)
            {
                jobpilot::test::CleanupJob(
                    pool.get(),
                    jobId);
            }

            for (std::uint64_t extraJobId : extraJobIds)
            {
                jobpilot::test::CleanupJob(
                    pool.get(),
                    extraJobId);
            }
        }

        std::string TrackEmail(
            const std::string &prefix)
        {
            const std::string email =
                jobpilot::test::UniqueEmail(prefix);
            emails.push_back(email);
            return email;
        }

        std::string CreateSecondUser()
        {
            const std::string email =
                TrackEmail("application-http-second");
            const std::optional<std::uint64_t> created =
                userDao->CreateUser(
                    "second",
                    email,
                    "hash");

            EXPECT_TRUE(created.has_value());

            const std::string secondToken =
                jobpilot::test::UniqueJobToken(
                    "application-token-second");
            tokens.push_back(secondToken);
            sessions->Set(secondToken, created.value());
            return secondToken;
        }

        ServerState state;
        std::thread serverThread;
        std::unique_ptr<
            jobpilot::core::ConnectionPool>
            pool;
        std::unique_ptr<jobpilot::core::RedisClient> redis;
        std::unique_ptr<jobpilot::core::SessionManager>
            sessions;
        std::unique_ptr<SessionAuthenticator>
            authenticator;
        std::unique_ptr<UserDao> userDao;
        std::unique_ptr<UserService> userService;
        std::unique_ptr<UserController> userController;
        std::unique_ptr<JobDao> jobDao;
        std::unique_ptr<ResumeDao> resumeDao;
        std::unique_ptr<ApplicationDao>
            applicationDao;
        std::unique_ptr<ApplicationService>
            applicationService;
        std::unique_ptr<ApplicationController>
            applicationController;
        std::uint64_t userId = 0;
        std::uint64_t jobId = 0;
        std::uint64_t resumeVersionId = 0;
        std::string token;
        std::vector<std::string> tokens;
        std::vector<std::string> emails;
        std::vector<std::uint64_t> extraJobIds;
    };

    TEST_F(ApplicationHttpTest, CreatesListsUpdatesAndStatistics)
    {
        const std::string authorization =
            "Bearer " + token;
        const nlohmann::json createBody{
            {"job_id", jobId},
            {"resume_version_id", resumeVersionId}};

        const std::optional<HttpResponseData> created =
            SendRequest(
                "POST",
                "/api/v1/applications",
                createBody.dump(),
                authorization);

        ASSERT_TRUE(created.has_value());
        ASSERT_TRUE(created->hasJson);
        EXPECT_EQ(created->statusCode, 200);
        EXPECT_EQ(created->jsonBody["data"]["status"], 0);

        const std::uint64_t applicationId =
            created->jsonBody["data"]["application_id"]
                .get<std::uint64_t>();

        const std::optional<HttpResponseData> duplicate =
            SendRequest(
                "POST",
                "/api/v1/applications",
                createBody.dump(),
                authorization);

        ASSERT_TRUE(duplicate.has_value());
        ASSERT_TRUE(duplicate->hasJson);
        EXPECT_EQ(duplicate->statusCode, 409);
        EXPECT_EQ(
            duplicate->jsonBody["code"],
            40004);

        const nlohmann::json status1{{"status", 1}};
        const std::optional<HttpResponseData> pending =
            SendRequest(
                "PUT",
                "/api/v1/applications/" +
                    std::to_string(applicationId) +
                    "/status",
                status1.dump(),
                authorization);

        ASSERT_TRUE(pending.has_value());
        EXPECT_EQ(pending->statusCode, 200);

        const nlohmann::json status2{{"status", 2}};
        const std::optional<HttpResponseData> applied =
            SendRequest(
                "PUT",
                "/api/v1/applications/" +
                    std::to_string(applicationId) +
                    "/status",
                status2.dump(),
                authorization);

        ASSERT_TRUE(applied.has_value());
        EXPECT_EQ(applied->statusCode, 200);

        const std::optional<HttpResponseData> list =
            SendRequest(
                "GET",
                "/api/v1/applications?status=2&page=1&page_size=20",
                "",
                authorization);

        ASSERT_TRUE(list.has_value());
        ASSERT_TRUE(list->hasJson);
        EXPECT_EQ(list->statusCode, 200);
        ASSERT_EQ(
            list->jsonBody["data"]["list"].size(),
            1U);
        EXPECT_FALSE(
            list->jsonBody["data"]["list"][0]["apply_time"]
                .is_null());

        const std::optional<HttpResponseData> statistics =
            SendRequest(
                "GET",
                "/api/v1/applications/statistics",
                "",
                authorization);

        ASSERT_TRUE(statistics.has_value());
        ASSERT_TRUE(statistics->hasJson);
        EXPECT_EQ(statistics->statusCode, 200);
        EXPECT_EQ(
            statistics->jsonBody["data"]["total"],
            1);
    }

    TEST_F(ApplicationHttpTest, RejectsAuthRelationshipsAndTransitions)
    {
        const std::optional<HttpResponseData> unauthenticated =
            SendRequest(
                "GET",
                "/api/v1/applications");

        ASSERT_TRUE(unauthenticated.has_value());
        EXPECT_EQ(unauthenticated->statusCode, 401);

        const std::optional<HttpResponseData> invalidToken =
            SendRequest(
                "GET",
                "/api/v1/applications",
                "",
                "Bearer invalid-token");

        ASSERT_TRUE(invalidToken.has_value());
        EXPECT_EQ(invalidToken->statusCode, 401);

        const std::string authorization =
            "Bearer " + token;
        const nlohmann::json missingJob{
            {"job_id", 999999999ULL},
            {"resume_version_id", resumeVersionId}};

        const std::optional<HttpResponseData> jobError =
            SendRequest(
                "POST",
                "/api/v1/applications",
                missingJob.dump(),
                authorization);

        ASSERT_TRUE(jobError.has_value());
        ASSERT_TRUE(jobError->hasJson);
        EXPECT_EQ(jobError->statusCode, 404);
        EXPECT_EQ(jobError->jsonBody["code"], 40001);

        // 0015A: a job that is still queued for review cannot be the target of
        // an application. The error is the same "job not found" an unknown id
        // produces, so the endpoint never confirms that a hidden job exists.
        const std::uint64_t queuedJobId =
            jobpilot::test::InsertJob(
                pool.get(),
                "Queued Co",
                "Queued Application Job",
                "Shenzhen",
                "description",
                jobpilot::test::UniqueJobToken(
                    "application-http-queued"),
                1,
                "2026-10-01 10:00:00");

        extraJobIds.push_back(queuedJobId);

        pool->Execute(
            "UPDATE jobs SET review_status = 1 WHERE id = ?",
            {static_cast<std::int64_t>(queuedJobId)});

        const nlohmann::json queuedJob{
            {"job_id", queuedJobId},
            {"resume_version_id", resumeVersionId}};

        const std::optional<HttpResponseData> queuedError =
            SendRequest(
                "POST",
                "/api/v1/applications",
                queuedJob.dump(),
                authorization);

        ASSERT_TRUE(queuedError.has_value());
        ASSERT_TRUE(queuedError->hasJson);
        EXPECT_EQ(queuedError->statusCode, 404);
        EXPECT_EQ(queuedError->jsonBody["code"], 40001);

        // Nothing was written for the queued job.
        const jobpilot::core::Result leaked =
            pool->Query(
                "SELECT id FROM applications WHERE job_id = ?",
                {queuedJobId});

        EXPECT_EQ(leaked.rowCount(), 0U);

        // The rollback switch is what makes it visible again: with
        // JOB_PUBLIC_INCLUDE_NEEDS_REVIEW=1 the same request is accepted. That is
        // asserted in the service-level test, which can inject the flag.

        const nlohmann::json missingResume{
            {"job_id", jobId},
            {"resume_version_id", 999999999ULL}};

        const std::optional<HttpResponseData> resumeError =
            SendRequest(
                "POST",
                "/api/v1/applications",
                missingResume.dump(),
                authorization);

        ASSERT_TRUE(resumeError.has_value());
        ASSERT_TRUE(resumeError->hasJson);
        EXPECT_EQ(resumeError->statusCode, 404);
        EXPECT_EQ(resumeError->jsonBody["code"], 40002);

        const nlohmann::json validBody{
            {"job_id", jobId},
            {"resume_version_id", resumeVersionId}};
        const std::optional<HttpResponseData> created =
            SendRequest(
                "POST",
                "/api/v1/applications",
                validBody.dump(),
                authorization);
        ASSERT_TRUE(created.has_value());
        ASSERT_EQ(created->statusCode, 200);
        const std::uint64_t applicationId =
            created->jsonBody["data"]["application_id"]
                .get<std::uint64_t>();

        const nlohmann::json invalidTransition{{"status", 2}};
        const std::optional<HttpResponseData> transition =
            SendRequest(
                "PUT",
                "/api/v1/applications/" +
                    std::to_string(applicationId) +
                    "/status",
                invalidTransition.dump(),
                authorization);

        ASSERT_TRUE(transition.has_value());
        ASSERT_TRUE(transition->hasJson);
        EXPECT_EQ(transition->statusCode, 400);
        EXPECT_EQ(
            transition->jsonBody["code"],
            40005);

        const std::string secondToken =
            CreateSecondUser();
        const std::optional<HttpResponseData> crossUser =
            SendRequest(
                "PUT",
                "/api/v1/applications/" +
                    std::to_string(applicationId) +
                    "/status",
                nlohmann::json{{"status", 1}}.dump(),
                "Bearer " + secondToken);

        ASSERT_TRUE(crossUser.has_value());
        ASSERT_TRUE(crossUser->hasJson);
        EXPECT_EQ(crossUser->statusCode, 404);
        EXPECT_EQ(crossUser->jsonBody["code"], 40003);
    }
} // namespace jobpilot::service
