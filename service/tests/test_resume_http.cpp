#include <arpa/inet.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
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
#include "jobpilot/service/resume/resume_controller.h"
#include "jobpilot/service/job/job_dao.h"
#include "jobpilot/service/resume/resume_artifact_dao.h"
#include "jobpilot/service/resume/resume_artifact_service.h"
#include "jobpilot/service/resume/resume_dao.h"
#include "jobpilot/service/resume/resume_service.h"
#include "jobpilot/service/router.h"
#include "jobpilot/service/user/user_controller.h"
#include "jobpilot/service/user/user_dao.h"
#include "jobpilot/service/user/user_service.h"
#include "jobpilot/storage/file_storage.h"
#include "test_user_support.h"
#include "http_test_timeout.h"

namespace jobpilot::service
{
    namespace
    {
        constexpr std::uint16_t kTestPort = 19093;
        constexpr const char *kBoundary =
            "jobpilot-resume-boundary";

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

        std::string MakeRequest(
            const std::string &method,
            const std::string &path,
            const std::string &body,
            const std::string &authorization,
            const std::string &contentType)
        {
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

            if (!contentType.empty())
            {
                request +=
                    "Content-Type: " +
                    contentType + "\r\n";
            }

            if (!body.empty())
            {
                request +=
                    "Content-Length: " +
                    std::to_string(body.size()) +
                    "\r\n";
            }

            request += "\r\n";
            request += body;

            return request;
        }

        std::optional<HttpResponseData> SendRequest(
            const std::string &method,
            const std::string &path,
            const std::string &body = "",
            const std::string &authorization = "",
            const std::string &contentType = "")
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

            if (!SendAll(
                    socketFd,
                    MakeRequest(
                        method,
                        path,
                        body,
                        authorization,
                        contentType)))
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

        std::string MakeMultipart(
            const std::string &title,
            const std::string &filename,
            const std::string &contentType,
            const std::string &bytes)
        {
            return
                "--" + std::string(kBoundary) +
                "\r\n"
                "Content-Disposition: form-data; name=\"title\"\r\n"
                "\r\n" +
                title +
                "\r\n"
                "--" + std::string(kBoundary) +
                "\r\n"
                "Content-Disposition: form-data; name=\"file\"; "
                "filename=\"" +
                filename +
                "\"\r\n"
                "Content-Type: " +
                contentType +
                "\r\n"
                "\r\n" +
                bytes +
                "\r\n"
                "--" + std::string(kBoundary) +
                "--\r\n";
        }

        std::string MakeStorageRoot()
        {
            static int counter = 0;

            return "/tmp/jobpilot-resume-http-" +
                   std::to_string(getpid()) +
                   "-" +
                   std::to_string(counter++);
        }
    } // namespace

    class ResumeHttpTest : public ::testing::Test
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
            resumeDao =
                std::make_unique<ResumeDao>(*pool);
            jobDao =
                std::make_unique<JobDao>(*pool);
            artifactDao =
                std::make_unique<ResumeArtifactDao>(
                    *pool,
                    *resumeDao);
            artifactService =
                std::make_unique<ResumeArtifactService>(
                    *artifactDao,
                    *resumeDao,
                    *userDao,
                    *jobDao,
                    *pool);

            storageRoot = MakeStorageRoot();
            storage =
                jobpilot::core::MakeLocalFileStorage(
                    storageRoot);

            resumeService =
                std::make_unique<ResumeService>(
                    *resumeDao,
                    *pool,
                    *storage,
                    *authenticator,
                    10U * 1024U * 1024U);
            resumeController =
                std::make_unique<ResumeController>(
                    *resumeService,
                    artifactService.get());

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
                            resumeController.get());

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

            for (const std::string &token : tokens)
            {
                redis->Del("session:" + token);
            }

            for (const std::string &email : emails)
            {
                jobpilot::test::CleanupUser(
                    pool.get(),
                    email);
            }

            if (!storageRoot.empty())
            {
                std::filesystem::remove_all(
                    storageRoot);
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

        std::pair<std::uint64_t, std::string>
        RegisterAndLogin(
            const std::string &prefix)
        {
            const std::string email =
                TrackEmail(prefix);
            const nlohmann::json registerBody{
                {"username", "tom"},
                {"email", email},
                {"password", "password"}};

            const std::optional<HttpResponseData> registration =
                SendRequest(
                    "POST",
                    "/api/v1/users/register",
                    registerBody.dump(),
                    "",
                    "application/json");

            if (!registration.has_value() ||
                !registration->hasJson ||
                registration->statusCode != 200)
            {
                throw std::runtime_error(
                    "registration failed");
            }

            const nlohmann::json loginBody{
                {"email", email},
                {"password", "password"}};

            const std::optional<HttpResponseData> login =
                SendRequest(
                    "POST",
                    "/api/v1/users/login",
                    loginBody.dump(),
                    "",
                    "application/json");

            if (!login.has_value() ||
                !login->hasJson ||
                login->statusCode != 200)
            {
                throw std::runtime_error(
                    "login failed");
            }

            const std::string token =
                login->jsonBody["data"]["token"]
                    .get<std::string>();
            tokens.push_back(token);

            return {
                registration->jsonBody["data"]["user_id"]
                    .get<std::uint64_t>(),
                token};
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
        std::unique_ptr<ResumeDao> resumeDao;
        std::unique_ptr<JobDao> jobDao;
        std::unique_ptr<ResumeArtifactDao> artifactDao;
        std::unique_ptr<ResumeArtifactService> artifactService;
        std::unique_ptr<jobpilot::core::FileStorage>
            storage;
        std::unique_ptr<ResumeService> resumeService;
        std::unique_ptr<ResumeController>
            resumeController;
        std::string storageRoot;
        std::vector<std::string> emails;
        std::vector<std::string> tokens;
    };

    TEST_F(ResumeHttpTest, UploadsListsAndCreatesVersions)
    {
        const auto user =
            RegisterAndLogin("resume-http-flow");
        const std::string authorization =
            "Bearer " + user.second;

        const std::string uploadBody =
            MakeMultipart(
                "C++ backend",
                "resume.txt",
                "text/plain",
                "hello resume");

        const std::optional<HttpResponseData> upload =
            SendRequest(
                "POST",
                "/api/v1/resumes/upload",
                uploadBody,
                authorization,
                "multipart/form-data; boundary=" +
                    std::string(kBoundary));

        ASSERT_TRUE(upload.has_value());
        ASSERT_TRUE(upload->hasJson);
        EXPECT_EQ(upload->statusCode, 200);
        EXPECT_EQ(upload->jsonBody["data"]["version"], 1);
        // 0015 A3 (option b): the response never leaves text availability
        // implicit.
        EXPECT_EQ(
            upload->jsonBody["data"]["text_extracted"],
            true);
        EXPECT_EQ(
            upload->jsonBody["data"]["text_status"],
            "ok");

        const std::uint64_t resumeId =
            upload->jsonBody["data"]["resume_id"]
                .get<std::uint64_t>();

        const std::optional<HttpResponseData> list =
            SendRequest(
                "GET",
                "/api/v1/resumes?page=1&page_size=20",
                "",
                authorization);

        ASSERT_TRUE(list.has_value());
        ASSERT_TRUE(list->hasJson);
        EXPECT_EQ(list->statusCode, 200);
        ASSERT_EQ(
            list->jsonBody["data"]["list"].size(),
            1U);
        EXPECT_EQ(
            list->jsonBody["data"]["list"][0]["id"],
            resumeId);

        const nlohmann::json versionBody{
            {"content", "second version"}};

        const std::optional<HttpResponseData> version =
            SendRequest(
                "POST",
                "/api/v1/resumes/" +
                    std::to_string(resumeId) +
                    "/versions",
                versionBody.dump(),
                authorization,
                "application/json");

        ASSERT_TRUE(version.has_value());
        ASSERT_TRUE(version->hasJson);
        EXPECT_EQ(version->statusCode, 200);
        EXPECT_EQ(version->jsonBody["data"]["version"], 2);
        EXPECT_EQ(
            version->jsonBody["data"]["text_extracted"],
            true);
        EXPECT_EQ(
            version->jsonBody["data"]["text_status"],
            "ok");

        const std::optional<HttpResponseData> versions =
            SendRequest(
                "GET",
                "/api/v1/resumes/" +
                    std::to_string(resumeId) +
                    "/versions",
                "",
                authorization);

        ASSERT_TRUE(versions.has_value());
        ASSERT_TRUE(versions->hasJson);
        EXPECT_EQ(versions->statusCode, 200);
        ASSERT_EQ(
            versions->jsonBody["data"]["list"].size(),
            2U);

        for (const auto &listed :
             versions->jsonBody["data"]["list"])
        {
            ASSERT_TRUE(listed.contains("text_status"));
            ASSERT_TRUE(listed.contains("text_extracted"));
            EXPECT_EQ(listed["text_status"], "ok");
            EXPECT_EQ(listed["text_extracted"], true);
        }
    }

    // 0015 A3 option (b): a PDF resume must never contribute nothing silently.
    TEST_F(ResumeHttpTest, ReportsExplicitPdfTextStatus)
    {
        const auto user =
            RegisterAndLogin("resume-http-pdf-status");
        const std::string authorization =
            "Bearer " + user.second;

        const std::string pdfBody =
            MakeMultipart(
                "PDF resume",
                "resume.pdf",
                "application/pdf",
                "%PDF-1.4\npdf");

        const std::optional<HttpResponseData> upload =
            SendRequest(
                "POST",
                "/api/v1/resumes/upload",
                pdfBody,
                authorization,
                "multipart/form-data; boundary=" +
                    std::string(kBoundary));

        ASSERT_TRUE(upload.has_value());
        ASSERT_TRUE(upload->hasJson);
        EXPECT_EQ(upload->statusCode, 200);
        EXPECT_EQ(
            upload->jsonBody["data"]["text_extracted"],
            false);
        EXPECT_EQ(
            upload->jsonBody["data"]["text_status"],
            "pdf_no_text");

        const std::uint64_t resumeId =
            upload->jsonBody["data"]["resume_id"]
                .get<std::uint64_t>();

        const std::optional<HttpResponseData> versions =
            SendRequest(
                "GET",
                "/api/v1/resumes/" +
                    std::to_string(resumeId) +
                    "/versions",
                "",
                authorization);

        ASSERT_TRUE(versions.has_value());
        ASSERT_TRUE(versions->hasJson);
        EXPECT_EQ(versions->statusCode, 200);
        ASSERT_EQ(
            versions->jsonBody["data"]["list"].size(),
            1U);

        const auto &listed =
            versions->jsonBody["data"]["list"][0];

        EXPECT_EQ(listed["text_extracted"], false);
        EXPECT_EQ(listed["text_status"], "pdf_no_text");
        // The version response never dumps the stored text either way.
        EXPECT_FALSE(listed.contains("content"));
    }

    TEST_F(ResumeHttpTest, RejectsAuthenticationAndMalformedInput)
    {
        const std::optional<HttpResponseData> unauthenticated =
            SendRequest(
                "GET",
                "/api/v1/resumes");

        ASSERT_TRUE(unauthenticated.has_value());
        ASSERT_TRUE(unauthenticated->hasJson);
        EXPECT_EQ(unauthenticated->statusCode, 401);

        const std::optional<HttpResponseData> invalidToken =
            SendRequest(
                "GET",
                "/api/v1/resumes",
                "",
                "Bearer invalid-token");

        ASSERT_TRUE(invalidToken.has_value());
        EXPECT_EQ(invalidToken->statusCode, 401);

        const auto user =
            RegisterAndLogin("resume-http-errors");
        const std::string authorization =
            "Bearer " + user.second;

        const std::optional<HttpResponseData> malformed =
            SendRequest(
                "POST",
                "/api/v1/resumes/upload",
                "not-multipart",
                authorization,
                "multipart/form-data; boundary=x");

        ASSERT_TRUE(malformed.has_value());
        ASSERT_TRUE(malformed->hasJson);
        EXPECT_EQ(malformed->statusCode, 400);
        EXPECT_EQ(malformed->jsonBody["code"], 10002);

        const std::string unsupportedBody =
            MakeMultipart(
                "DOCX",
                "resume.docx",
                "application/vnd.openxmlformats-officedocument."
                "wordprocessingml.document",
                "docx");

        const std::optional<HttpResponseData> unsupported =
            SendRequest(
                "POST",
                "/api/v1/resumes/upload",
                unsupportedBody,
                authorization,
                "multipart/form-data; boundary=" +
                    std::string(kBoundary));

        ASSERT_TRUE(unsupported.has_value());
        ASSERT_TRUE(unsupported->hasJson);
        EXPECT_EQ(unsupported->statusCode, 400);
        EXPECT_EQ(unsupported->jsonBody["code"], 10001);
    }

    TEST_F(ResumeHttpTest, EnforcesCrossUserIsolation)
    {
        const auto firstUser =
            RegisterAndLogin("resume-http-first");
        const auto secondUser =
            RegisterAndLogin("resume-http-second");

        const std::string uploadBody =
            MakeMultipart(
                "private",
                "resume.md",
                "text/markdown",
                "# private");

        const std::optional<HttpResponseData> upload =
            SendRequest(
                "POST",
                "/api/v1/resumes/upload",
                uploadBody,
                "Bearer " + firstUser.second,
                "multipart/form-data; boundary=" +
                    std::string(kBoundary));

        ASSERT_TRUE(upload.has_value());
        ASSERT_TRUE(upload->hasJson);
        ASSERT_EQ(upload->statusCode, 200);

        const std::uint64_t resumeId =
            upload->jsonBody["data"]["resume_id"]
                .get<std::uint64_t>();

        const nlohmann::json versionBody{
            {"content", "cross-user"}};

        const std::optional<HttpResponseData> crossVersion =
            SendRequest(
                "POST",
                "/api/v1/resumes/" +
                    std::to_string(resumeId) +
                    "/versions",
                versionBody.dump(),
                "Bearer " + secondUser.second,
                "application/json");

        ASSERT_TRUE(crossVersion.has_value());
        ASSERT_TRUE(crossVersion->hasJson);
        EXPECT_EQ(crossVersion->statusCode, 404);
        EXPECT_EQ(
            crossVersion->jsonBody["code"],
            40002);

        const std::optional<HttpResponseData> secondList =
            SendRequest(
                "GET",
                "/api/v1/resumes",
                "",
                "Bearer " + secondUser.second);

        ASSERT_TRUE(secondList.has_value());
        ASSERT_TRUE(secondList->hasJson);
        EXPECT_TRUE(
            secondList->jsonBody["data"]["list"].empty());
    }

    TEST_F(ResumeHttpTest, ManagesTailoredArtifactsWithinTheConfirmationBoundary)
    {
        const auto user =
            RegisterAndLogin("resume-artifact-http");
        const std::string authorization =
            "Bearer " + user.second;

        std::uint64_t versionId = 0;

        {
            auto transaction = pool->BeginTransaction();

            const std::uint64_t resumeId =
                resumeDao
                    ->CreateResume(
                        transaction,
                        user.first,
                        "artifact base")
                    .value();

            ASSERT_TRUE(
                resumeDao->LockOwnedResume(
                    transaction,
                    user.first,
                    resumeId));

            versionId =
                resumeDao
                    ->CreateVersion(
                        transaction,
                        resumeId,
                        1,
                        "1/resumes/artifact/v1.txt",
                        std::string("原始简历正文"))
                    .value();

            transaction.Commit();
        }

        const jobpilot::core::Result versionBefore =
            pool->Query(
                "SELECT content FROM resume_versions WHERE id = ?",
                {versionId});

        ASSERT_EQ(versionBefore.rowCount(), 1U);

        const nlohmann::json createBody{
            {"base_version_id", versionId}};

        const std::optional<HttpResponseData> created =
            SendRequest(
                "POST",
                "/api/v1/resumes/artifacts",
                createBody.dump(),
                authorization,
                "application/json");

        ASSERT_TRUE(created.has_value());
        ASSERT_TRUE(created->hasJson);
        EXPECT_EQ(created->statusCode, 200);
        EXPECT_EQ(created->jsonBody["data"]["confirmed"], false);
        EXPECT_TRUE(
            created->jsonBody["data"]["confirmed_at"].is_null());
        EXPECT_EQ(
            created->jsonBody["data"]["fact_check_status"],
            1);
        EXPECT_EQ(
            created->jsonBody["data"]["generator"],
            "rule_v1");
        EXPECT_EQ(
            created->jsonBody["data"]["source_version"],
            "tailor_rule_v1");
        EXPECT_TRUE(
            created->jsonBody["data"]["fact_check_report"]
                .is_object());
        EXPECT_EQ(
            created->jsonBody["data"]["base_version_id"],
            versionId);

        const std::string artifactContent =
            created->jsonBody["data"]["content"]
                .get<std::string>();

        // Name/contact are not derivable from the delivered seams, so the draft
        // states them as explicit placeholders instead of inventing them.
        EXPECT_NE(
            artifactContent.find("【请填写：姓名】"),
            std::string::npos);
        EXPECT_NE(
            artifactContent.find("【请填写：联系方式】"),
            std::string::npos);

        const std::uint64_t artifactId =
            created->jsonBody["data"]["id"]
                .get<std::uint64_t>();
        const std::string artifactPath =
            "/api/v1/resumes/artifacts/" +
            std::to_string(artifactId);

        const std::optional<HttpResponseData> list =
            SendRequest(
                "GET",
                "/api/v1/resumes/artifacts",
                "",
                authorization);

        ASSERT_TRUE(list.has_value());
        ASSERT_TRUE(list->hasJson);
        EXPECT_EQ(list->statusCode, 200);
        ASSERT_EQ(list->jsonBody["data"]["list"].size(), 1U);

        const std::optional<HttpResponseData> got =
            SendRequest("GET", artifactPath, "", authorization);

        ASSERT_TRUE(got.has_value());
        ASSERT_TRUE(got->hasJson);
        EXPECT_EQ(got->statusCode, 200);
        EXPECT_EQ(
            got->jsonBody["data"]["id"],
            artifactId);
        EXPECT_EQ(got->jsonBody["data"]["confirmed"], false);

        const std::optional<HttpResponseData> confirmed =
            SendRequest(
                "POST",
                artifactPath + "/confirm",
                "{}",
                authorization,
                "application/json");

        ASSERT_TRUE(confirmed.has_value());
        ASSERT_TRUE(confirmed->hasJson);
        EXPECT_EQ(confirmed->statusCode, 200);
        EXPECT_EQ(
            confirmed->jsonBody["data"]["confirmed"],
            true);
        EXPECT_FALSE(
            confirmed->jsonBody["data"]["confirmed_at"].is_null());

        // Confirmation is a one-way state change.
        const std::optional<HttpResponseData> again =
            SendRequest(
                "POST",
                artifactPath + "/confirm",
                "{}",
                authorization,
                "application/json");

        ASSERT_TRUE(again.has_value());
        EXPECT_EQ(again->statusCode, 409);

        // Ownership: another user cannot base an artifact on this version nor
        // read this artifact.
        const auto other =
            RegisterAndLogin("resume-artifact-other");

        const std::optional<HttpResponseData> foreign =
            SendRequest(
                "POST",
                "/api/v1/resumes/artifacts",
                createBody.dump(),
                "Bearer " + other.second,
                "application/json");

        ASSERT_TRUE(foreign.has_value());
        EXPECT_EQ(foreign->statusCode, 404);

        const std::optional<HttpResponseData> foreignRead =
            SendRequest(
                "GET",
                artifactPath,
                "",
                "Bearer " + other.second);

        ASSERT_TRUE(foreignRead.has_value());
        EXPECT_EQ(foreignRead->statusCode, 404);

        // A failed fact gate blocks confirmation at the endpoint level.
        const jobpilot::core::ExecutionResult tampered =
            pool->Execute(
                "INSERT INTO resume_artifacts "
                "(user_id, base_version_id, artifact_type, language, "
                "content, content_bytes, generator, source_version, "
                "fact_check_status, fact_check_report) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                {user.first,
                 versionId,
                 std::string("resume"),
                 std::string("zh"),
                 std::string("草稿"),
                 std::uint64_t(6),
                 std::string("rule_v1"),
                 std::string("tailor_rule_v1"),
                 std::int64_t(2),
                 std::string("{\"status\": \"fail\"}")});

        ASSERT_EQ(tampered.affectedRows, 1U);

        const std::optional<HttpResponseData> blocked =
            SendRequest(
                "POST",
                "/api/v1/resumes/artifacts/" +
                    std::to_string(tampered.lastInsertId) +
                    "/confirm",
                "{}",
                authorization,
                "application/json");

        ASSERT_TRUE(blocked.has_value());
        EXPECT_EQ(blocked->statusCode, 409);

        // 0015 boundary: nothing here sends the artifact anywhere -- the probe
        // below asserts those verbs have no route at all.
        for (const char *suffix : {"/submit", "/apply", "/send"})
        {
            const std::optional<HttpResponseData> probe =
                SendRequest(
                    "POST",
                    artifactPath + suffix,
                    "{}",
                    authorization,
                    "application/json");

            ASSERT_TRUE(probe.has_value()) << suffix;
            EXPECT_EQ(probe->statusCode, 404) << suffix;
        }

        // The base resume version text was never written by the artifact path.
        const jobpilot::core::Result versionAfter =
            pool->Query(
                "SELECT content FROM resume_versions WHERE id = ?",
                {versionId});

        ASSERT_EQ(versionAfter.rowCount(), 1U);
        EXPECT_EQ(
            versionAfter.value(0, 0).value(),
            versionBefore.value(0, 0).value());
        EXPECT_EQ(
            versionAfter.value(0, 0).value(),
            "原始简历正文");

        pool->Execute(
            "DELETE FROM resume_artifacts WHERE user_id IN (?, ?)",
            {user.first, other.first});
    }

} // namespace jobpilot::service
