#include <arpa/inet.h>

#include <chrono>
#include <condition_variable>
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
#include "jobpilot/service/application/application_dao.h"
#include "jobpilot/service/application/application_service.h"
#include "jobpilot/service/auth/session_authenticator.h"
#include "jobpilot/service/job/internal_job_import_controller.h"
#include "jobpilot/service/job/job_controller.h"
#include "jobpilot/service/job/job_dao.h"
#include "jobpilot/service/job/job_ingestion_dao.h"
#include "jobpilot/service/job/job_ingestion_service.h"
#include "jobpilot/service/job/job_service.h"
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
        constexpr std::uint16_t kTestPort = 19096;

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

        nlohmann::json MakeJobJson(
            const std::string &sourceUrl,
            const std::string &title)
        {
            return nlohmann::json{
                {"company", "Import Company"},
                {"title", title},
                {"location", "Shenzhen"},
                {"salary_min", 20},
                {"salary_max", 35},
                {"salary_currency", "CNY"},
                {"description", "Imported description"},
                {"source", "import"},
                {"source_url", sourceUrl},
                {"requirements",
                 nlohmann::json::array(
                     {{{"skill", "C++"}, {"level", 3}},
                      {{"skill", "MySQL"}, {"level", 2}},
                      {{"skill", "Redis"}, {"level", 2}}})}};
        }
    } // namespace

    class JobIngestionHttpTest : public ::testing::Test
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
            jobService =
                std::make_unique<JobService>(*jobDao);
            jobController =
                std::make_unique<JobController>(
                    *jobService);
            ingestionDao =
                std::make_unique<JobIngestionDao>();
            ingestionService =
                std::make_unique<JobIngestionService>(
                    *ingestionDao,
                    *pool);

            internalToken =
                jobpilot::test::UniqueJobToken(
                    "internal-token");
            internalController =
                std::make_unique<
                    InternalJobImportController>(
                    *ingestionService,
                    internalToken);

            resumeDao =
                std::make_unique<ResumeDao>(
                    *pool);
            applicationDao =
                std::make_unique<ApplicationDao>(
                    *pool);
            applicationService =
                std::make_unique<ApplicationService>(
                    *applicationDao,
                    *jobDao,
                    *resumeDao,
                    *pool,
                    *authenticator);

            email =
                jobpilot::test::UniqueEmail(
                    "job-ingestion-http");
            const std::optional<std::uint64_t> createdUser =
                userDao->CreateUser(
                    "job-ingestion-http-user",
                    email,
                    "hash");
            ASSERT_TRUE(createdUser.has_value());
            userId = createdUser.value();

            const jobpilot::test::ResumeVersionFixture resume =
                jobpilot::test::InsertResumeVersion(
                    pool.get(),
                    userId,
                    "Job Ingestion Resume",
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
                            jobController.get(),
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

            for (const std::string &sourceUrl :
                 sourceUrls)
            {
                const jobpilot::core::Result jobs =
                    pool->Query(
                        "SELECT id FROM jobs WHERE source_url = ?",
                        {sourceUrl});

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

            jobpilot::test::CleanupUser(
                pool.get(),
                email);
        }

        std::unique_ptr<
            jobpilot::core::ConnectionPool>
            pool;
        std::unique_ptr<jobpilot::core::RedisClient>
            redis;
        std::unique_ptr<jobpilot::core::SessionManager>
            sessions;
        std::unique_ptr<SessionAuthenticator>
            authenticator;
        std::unique_ptr<UserDao> userDao;
        std::unique_ptr<UserService> userService;
        std::unique_ptr<UserController> userController;
        std::unique_ptr<JobDao> jobDao;
        std::unique_ptr<JobService> jobService;
        std::unique_ptr<JobController> jobController;
        std::unique_ptr<JobIngestionDao> ingestionDao;
        std::unique_ptr<JobIngestionService>
            ingestionService;
        std::unique_ptr<InternalJobImportController>
            internalController;
        std::unique_ptr<ResumeDao> resumeDao;
        std::unique_ptr<ApplicationDao> applicationDao;
        std::unique_ptr<ApplicationService>
            applicationService;
        ServerState state;
        std::thread serverThread;
        std::string internalToken;
        std::string email;
        std::uint64_t userId = 0;
        std::uint64_t resumeVersionId = 0;
        std::vector<std::string> sourceUrls;

        // 0015A: source_url must be an absolute http(s) job-detail URL
        // (ExecPlan 0015A section 5.5.1); the fixtures below use this shape.
        std::string UniqueUrl(
            const std::string &prefix)
        {
            return "https://example.invalid/job/" +
                   jobpilot::test::UniqueJobToken(prefix);
        }
    };

    TEST_F(JobIngestionHttpTest, RejectsAuthenticationAndInvalidRequests)
    {
        const std::string sourceUrl =
            UniqueUrl(
                "http-auth");
        sourceUrls.push_back(sourceUrl);
        const std::string body =
            nlohmann::json{
                {"jobs",
                 nlohmann::json::array(
                     {MakeJobJson(sourceUrl, "Auth Job")})}}
                .dump();

        const auto missing =
            SendRequest(
                "POST",
                "/internal/jobs/import",
                body,
                "");

        ASSERT_TRUE(missing.has_value());
        ASSERT_TRUE(missing->hasJson);
        EXPECT_EQ(missing->statusCode, 401);
        EXPECT_EQ(missing->jsonBody["code"], 20001);

        const auto wrong =
            SendRequest(
                "POST",
                "/internal/jobs/import",
                body,
                "wrong-token");

        ASSERT_TRUE(wrong.has_value());
        ASSERT_TRUE(wrong->hasJson);
        EXPECT_EQ(wrong->statusCode, 401);
        EXPECT_EQ(wrong->jsonBody["code"], 20001);

        const auto malformed =
            SendRequest(
                "POST",
                "/internal/jobs/import",
                "{invalid",
                internalToken);

        ASSERT_TRUE(malformed.has_value());
        ASSERT_TRUE(malformed->hasJson);
        EXPECT_EQ(malformed->statusCode, 400);
        EXPECT_EQ(malformed->jsonBody["code"], 10002);

        const auto empty =
            SendRequest(
                "POST",
                "/internal/jobs/import",
                "{\"jobs\":[]}",
                internalToken);

        ASSERT_TRUE(empty.has_value());
        ASSERT_TRUE(empty->hasJson);
        EXPECT_EQ(empty->statusCode, 400);
        EXPECT_EQ(empty->jsonBody["code"], 10001);

        nlohmann::json tooMany =
            nlohmann::json::array();

        for (int index = 0; index < 101; ++index)
        {
            tooMany.push_back(
                MakeJobJson(
                    UniqueUrl(
                        "http-too-many"),
                    "Too Many " +
                        std::to_string(index)));
        }

        const auto batchTooLarge =
            SendRequest(
                "POST",
                "/internal/jobs/import",
                nlohmann::json{
                    {"jobs", std::move(tooMany)}}
                    .dump(),
                internalToken);

        ASSERT_TRUE(batchTooLarge.has_value());
        ASSERT_TRUE(batchTooLarge->hasJson);
        EXPECT_EQ(batchTooLarge->statusCode, 400);
        EXPECT_EQ(batchTooLarge->jsonBody["code"], 10001);

        nlohmann::json negativeSalary =
            MakeJobJson(
                UniqueUrl(
                    "http-negative-salary"),
                "Negative Salary");
        negativeSalary["salary_min"] = -1;

        const auto invalidSalary =
            SendRequest(
                "POST",
                "/internal/jobs/import",
                nlohmann::json{
                    {"jobs",
                     nlohmann::json::array(
                         {negativeSalary})}}
                    .dump(),
                internalToken);

        ASSERT_TRUE(invalidSalary.has_value());
        ASSERT_TRUE(invalidSalary->hasJson);
        EXPECT_EQ(invalidSalary->statusCode, 400);
        EXPECT_EQ(invalidSalary->jsonBody["code"], 10001);
    }

    TEST_F(JobIngestionHttpTest, ImportsIdempotentlyAndCompletesClosure)
    {
        const std::string sourceUrl =
            UniqueUrl(
                "http-idempotent");
        sourceUrls.push_back(sourceUrl);

        const auto created =
            SendRequest(
                "POST",
                "/internal/jobs/import",
                nlohmann::json{
                    {"jobs",
                     nlohmann::json::array(
                         {MakeJobJson(sourceUrl, "Before")})}}
                    .dump(),
                internalToken);

        ASSERT_TRUE(created.has_value());
        ASSERT_TRUE(created->hasJson);
        EXPECT_EQ(created->statusCode, 200);
        EXPECT_EQ(created->jsonBody["created"], 1);
        EXPECT_EQ(created->jsonBody["updated"], 0);
        EXPECT_EQ(created->jsonBody["total"], 1);

        const jobpilot::core::Result stored =
            pool->Query(
                "SELECT id FROM jobs WHERE source_url = ?",
                {sourceUrl});
        ASSERT_EQ(stored.rowCount(), 1U);
        ASSERT_TRUE(stored.value(0, 0).has_value());

        const std::uint64_t jobId =
            static_cast<std::uint64_t>(
                std::stoull(
                    stored.value(0, 0).value()));

        const auto detail =
            SendRequest(
                "GET",
                "/api/v1/jobs/" +
                    std::to_string(jobId),
                "",
                "");

        ASSERT_TRUE(detail.has_value());
        ASSERT_TRUE(detail->hasJson);
        EXPECT_EQ(detail->statusCode, 200);
        EXPECT_EQ(
            detail->jsonBody["data"]["title"],
            "Before");

        nlohmann::json updatedJob =
            MakeJobJson(sourceUrl, "After");
        updatedJob["requirements"] =
            nlohmann::json::array(
                {{{"skill", "C++"}, {"level", 3}},
                 {{"skill", "PostgreSQL"}, {"level", 2}}});

        const auto updated =
            SendRequest(
                "POST",
                "/internal/jobs/import",
                nlohmann::json{
                    {"jobs",
                     nlohmann::json::array(
                         {updatedJob})}}
                    .dump(),
                internalToken);

        ASSERT_TRUE(updated.has_value());
        ASSERT_TRUE(updated->hasJson);
        EXPECT_EQ(updated->statusCode, 200);
        EXPECT_EQ(updated->jsonBody["created"], 0);
        EXPECT_EQ(updated->jsonBody["updated"], 1);
        EXPECT_EQ(updated->jsonBody["total"], 1);

        const auto updatedDetail =
            SendRequest(
                "GET",
                "/api/v1/jobs/" +
                    std::to_string(jobId),
                "",
                "");

        ASSERT_TRUE(updatedDetail.has_value());
        ASSERT_TRUE(updatedDetail->hasJson);
        ASSERT_EQ(
            updatedDetail->jsonBody["data"]["requirements"]
                .size(),
            2U);
        EXPECT_EQ(
            updatedDetail->jsonBody["data"]["title"],
            "After");
        EXPECT_EQ(
            updatedDetail->jsonBody["data"]["requirements"][0]
                                   ["skill"],
            "C++");
        EXPECT_EQ(
            updatedDetail->jsonBody["data"]["requirements"][1]
                                   ["skill"],
            "PostgreSQL");

        const ServiceResult<ApplicationCreateData> application =
            applicationService->Create(
                userId,
                jobId,
                resumeVersionId);

        EXPECT_EQ(
            application.code,
            ErrorCode::kSuccess);
        EXPECT_GT(
            application.value.applicationId,
            0U);
    }

    TEST_F(JobIngestionHttpTest, RollsBackWholeBatch)
    {
        const std::string validUrl =
            UniqueUrl(
                "http-rollback-valid");
        const std::string invalidUrl =
            UniqueUrl(
                "http-rollback-invalid");
        sourceUrls.push_back(validUrl);
        sourceUrls.push_back(invalidUrl);

        nlohmann::json invalidJob =
            MakeJobJson(
                invalidUrl,
                "Invalid Job");
        invalidJob["requirements"] =
            nlohmann::json::array(
                {{{"skill", "C++"}, {"level", 4}}});

        const auto result =
            SendRequest(
                "POST",
                "/internal/jobs/import",
                nlohmann::json{
                    {"jobs",
                     nlohmann::json::array(
                         {MakeJobJson(
                              validUrl,
                              "Valid Job"),
                          invalidJob})}}
                    .dump(),
                internalToken);

        ASSERT_TRUE(result.has_value());
        ASSERT_TRUE(result->hasJson);
        EXPECT_EQ(result->statusCode, 400);
        EXPECT_EQ(result->jsonBody["code"], 10001);

        for (const std::string &sourceUrl :
             {validUrl, invalidUrl})
        {
            const jobpilot::core::Result count =
                pool->Query(
                    "SELECT COUNT(*) FROM jobs "
                    "WHERE source_url = ?",
                    {sourceUrl});

            ASSERT_EQ(count.rowCount(), 1U);
            ASSERT_TRUE(count.value(0, 0).has_value());
            EXPECT_EQ(count.value(0, 0).value(), "0");
        }
    }

    TEST_F(JobIngestionHttpTest, Validates0014ExtraFieldValues)
    {
        auto statusFor =
            [this](nlohmann::json job)
        {
            sourceUrls.push_back(
                job["source_url"].get<std::string>());

            const auto response =
                SendRequest(
                    "POST",
                    "/internal/jobs/import",
                    nlohmann::json{
                        {"jobs",
                         nlohmann::json::array({job})}}
                        .dump(),
                    internalToken);

            return response.has_value()
                       ? response->statusCode
                       : 0;
        };

        nlohmann::json base = MakeJobJson(
            UniqueUrl("extra-base"),
            "Extra Base");

        nlohmann::json educationTooHigh = base;
        educationTooHigh["source_url"] =
            UniqueUrl("extra-education-high");
        educationTooHigh["education_required"] = 6;
        EXPECT_EQ(statusFor(educationTooHigh), 400);

        nlohmann::json educationTooLow = base;
        educationTooLow["source_url"] =
            UniqueUrl("extra-education-low");
        educationTooLow["education_required"] = 0;
        EXPECT_EQ(statusFor(educationTooLow), 400);

        nlohmann::json educationNegative = base;
        educationNegative["source_url"] =
            UniqueUrl("extra-education-negative");
        educationNegative["education_required"] = -1;
        EXPECT_EQ(statusFor(educationNegative), 400);

        nlohmann::json natureTooHigh = base;
        natureTooHigh["source_url"] =
            UniqueUrl("extra-nature-high");
        natureTooHigh["job_nature"] = 4;
        EXPECT_EQ(statusFor(natureTooHigh), 400);

        nlohmann::json invertedYears = base;
        invertedYears["source_url"] =
            UniqueUrl("extra-years-inverted");
        invertedYears["experience_years_min"] = 5;
        invertedYears["experience_years_max"] = 3;
        EXPECT_EQ(statusFor(invertedYears), 400);

        nlohmann::json yearsOverflow = base;
        yearsOverflow["source_url"] =
            UniqueUrl("extra-years-overflow");
        yearsOverflow["experience_years_max"] = 32768;
        EXPECT_EQ(statusFor(yearsOverflow), 400);

        nlohmann::json malformedDeadline = base;
        malformedDeadline["source_url"] =
            UniqueUrl("extra-deadline-malformed");
        malformedDeadline["deadline"] = "2026-10-31T10:30:00";
        EXPECT_EQ(statusFor(malformedDeadline), 400);

        nlohmann::json impossibleDeadline = base;
        impossibleDeadline["source_url"] =
            UniqueUrl("extra-deadline-impossible");
        impossibleDeadline["deadline"] = "2026-02-29 10:30:00";
        EXPECT_EQ(statusFor(impossibleDeadline), 400);

        nlohmann::json dateOnlyWithInstant = base;
        dateOnlyWithInstant["source_url"] =
            UniqueUrl("extra-date-only-instant");
        dateOnlyWithInstant["deadline"] = "2026-10-31 10:30:00";
        dateOnlyWithInstant["deadline_date_only"] = true;
        EXPECT_EQ(statusFor(dateOnlyWithInstant), 400);

        nlohmann::json flagWithoutDeadline = base;
        flagWithoutDeadline["source_url"] =
            UniqueUrl("extra-flag-only");
        flagWithoutDeadline["deadline_date_only"] = true;
        EXPECT_EQ(statusFor(flagWithoutDeadline), 400);

        nlohmann::json nonBooleanFlag = base;
        nonBooleanFlag["source_url"] =
            UniqueUrl("extra-flag-type");
        nonBooleanFlag["deadline"] = "2026-10-31 04:00:00";
        nonBooleanFlag["deadline_date_only"] = "yes";
        EXPECT_EQ(statusFor(nonBooleanFlag), 400);

        nlohmann::json dateOnlyValid = base;
        dateOnlyValid["source_url"] =
            UniqueUrl("extra-date-only-valid");
        dateOnlyValid["education_required"] = 3;
        dateOnlyValid["experience_years_min"] = 0;
        dateOnlyValid["experience_years_max"] = 3;
        dateOnlyValid["job_nature"] = 1;
        dateOnlyValid["deadline"] = "2026-10-31 04:00:00";
        dateOnlyValid["deadline_date_only"] = true;
        EXPECT_EQ(statusFor(dateOnlyValid), 200);

        nlohmann::json explicitValid = base;
        explicitValid["source_url"] =
            UniqueUrl("extra-explicit-valid");
        explicitValid["deadline"] = "2026-10-31 10:30:00";
        explicitValid["deadline_date_only"] = false;
        EXPECT_EQ(statusFor(explicitValid), 200);
    }

    TEST_F(JobIngestionHttpTest, RoundTripsBothDeadlineKindsThroughApi)
    {
        const std::string dateOnlyUrl =
            UniqueUrl("extra-roundtrip-date");
        sourceUrls.push_back(dateOnlyUrl);

        nlohmann::json dateOnly =
            MakeJobJson(dateOnlyUrl, "Roundtrip Date Only");
        dateOnly["education_required"] = 3;
        dateOnly["experience_years_min"] = 0;
        dateOnly["experience_years_max"] = 3;
        dateOnly["job_nature"] = 1;
        dateOnly["deadline"] = "2026-10-31 04:00:00";
        dateOnly["deadline_date_only"] = true;
        // 0017A: the optional publication instant travels through the import
        // payload and comes back as the public posted_at.
        dateOnly["published_at"] = "2026-09-20 00:00:00";

        const auto created =
            SendRequest(
                "POST",
                "/internal/jobs/import",
                nlohmann::json{
                    {"jobs",
                     nlohmann::json::array({dateOnly})}}
                    .dump(),
                internalToken);

        ASSERT_TRUE(created.has_value());
        ASSERT_TRUE(created->hasJson);
        ASSERT_EQ(created->statusCode, 200);

        const jobpilot::core::Result stored =
            pool->Query(
                "SELECT id, deadline, deadline_date_only, "
                "education_required, experience_years_min, "
                "experience_years_max, job_nature, published_at "
                "FROM jobs WHERE source_url = ?",
                {dateOnlyUrl});

        ASSERT_EQ(stored.rowCount(), 1U);

        // The date-only convention is stored as Beijing noon in UTC.
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 1)),
            "2026-10-31 04:00:00");
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 2)),
            "1");
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 3)),
            "3");
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 4)),
            "0");
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 5)),
            "3");
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 6)),
            "1");
        // 0017A: the publication instant reached the row.
        EXPECT_EQ(
            jobpilot::test::RequireValue(stored.value(0, 7)),
            "2026-09-20 00:00:00");

        const std::uint64_t jobId =
            static_cast<std::uint64_t>(
                std::stoull(
                    jobpilot::test::RequireValue(
                        stored.value(0, 0))));

        const auto detail =
            SendRequest(
                "GET",
                "/api/v1/jobs/" + std::to_string(jobId),
                "",
                "");

        ASSERT_TRUE(detail.has_value());
        ASSERT_TRUE(detail->hasJson);
        ASSERT_EQ(detail->statusCode, 200);
        EXPECT_EQ(
            detail->jsonBody["data"]["education_required"],
            3);
        EXPECT_EQ(
            detail->jsonBody["data"]["experience_years_min"],
            0);
        EXPECT_EQ(
            detail->jsonBody["data"]["experience_years_max"],
            3);
        EXPECT_EQ(detail->jsonBody["data"]["job_nature"], 1);

        // The date-only value renders as a Beijing calendar date, never as the
        // 12:00 storage convention.
        EXPECT_EQ(
            detail->jsonBody["data"]["deadline"],
            "2026-10-31");
        EXPECT_EQ(
            detail->jsonBody["data"]["deadline_date_only"],
            true);
        // 0017A: the public detail exposes the stored instant as posted_at.
        EXPECT_EQ(
            detail->jsonBody["data"]["posted_at"],
            "2026-09-20 00:00:00");

        const std::string keyword = dateOnlyUrl.substr(
            dateOnlyUrl.rfind('/') + 1);

        nlohmann::json searchJob =
            MakeJobJson(dateOnlyUrl, keyword);
        searchJob["education_required"] = 3;
        searchJob["deadline"] = "2026-10-31 04:00:00";
        searchJob["deadline_date_only"] = true;

        const auto updated =
            SendRequest(
                "POST",
                "/internal/jobs/import",
                nlohmann::json{
                    {"jobs",
                     nlohmann::json::array({searchJob})}}
                    .dump(),
                internalToken);

        ASSERT_TRUE(updated.has_value());
        ASSERT_EQ(updated->statusCode, 200);

        const auto search =
            SendRequest(
                "GET",
                "/api/v1/jobs?keyword=" + keyword,
                "",
                "");

        ASSERT_TRUE(search.has_value());
        ASSERT_TRUE(search->hasJson);
        ASSERT_EQ(search->statusCode, 200);
        ASSERT_EQ(search->jsonBody["data"]["list"].size(), 1U);
        EXPECT_EQ(
            search->jsonBody["data"]["list"][0]["education_required"],
            3);
        EXPECT_EQ(
            search->jsonBody["data"]["list"][0]["deadline"],
            "2026-10-31");
        EXPECT_EQ(
            search->jsonBody["data"]["list"][0]["deadline_date_only"],
            true);

        // Explicit time is preserved and rendered as Beijing local time.
        const std::string explicitUrl =
            UniqueUrl("extra-roundtrip-explicit");
        sourceUrls.push_back(explicitUrl);

        nlohmann::json explicitJob =
            MakeJobJson(explicitUrl, "Roundtrip Explicit");
        explicitJob["deadline"] = "2026-10-31 10:30:00";
        explicitJob["deadline_date_only"] = false;

        const auto explicitCreated =
            SendRequest(
                "POST",
                "/internal/jobs/import",
                nlohmann::json{
                    {"jobs",
                     nlohmann::json::array({explicitJob})}}
                    .dump(),
                internalToken);

        ASSERT_TRUE(explicitCreated.has_value());
        ASSERT_EQ(explicitCreated->statusCode, 200);

        const jobpilot::core::Result explicitStored =
            pool->Query(
                "SELECT id, deadline, deadline_date_only "
                "FROM jobs WHERE source_url = ?",
                {explicitUrl});

        ASSERT_EQ(explicitStored.rowCount(), 1U);
        EXPECT_EQ(
            jobpilot::test::RequireValue(
                explicitStored.value(0, 1)),
            "2026-10-31 10:30:00");
        EXPECT_EQ(
            jobpilot::test::RequireValue(
                explicitStored.value(0, 2)),
            "0");

        const std::uint64_t explicitJobId =
            static_cast<std::uint64_t>(
                std::stoull(
                    jobpilot::test::RequireValue(
                        explicitStored.value(0, 0))));

        const auto explicitDetail =
            SendRequest(
                "GET",
                "/api/v1/jobs/" + std::to_string(explicitJobId),
                "",
                "");

        ASSERT_TRUE(explicitDetail.has_value());
        ASSERT_TRUE(explicitDetail->hasJson);
        EXPECT_EQ(
            explicitDetail->jsonBody["data"]["deadline"],
            "2026-10-31 18:30:00");
        EXPECT_EQ(
            explicitDetail->jsonBody["data"]["deadline_date_only"],
            false);

        // The manual path is a whole-group overwrite: re-importing without a
        // deadline clears it, unlike the observation path's COALESCE guard.
        nlohmann::json cleared = MakeJobJson(dateOnlyUrl, keyword);
        cleared["education_required"] = 3;

        const auto clearedResult =
            SendRequest(
                "POST",
                "/internal/jobs/import",
                nlohmann::json{
                    {"jobs",
                     nlohmann::json::array({cleared})}}
                    .dump(),
                internalToken);

        ASSERT_TRUE(clearedResult.has_value());
        ASSERT_EQ(clearedResult->statusCode, 200);

        const jobpilot::core::Result clearedStored =
            pool->Query(
                "SELECT deadline, deadline_date_only "
                "FROM jobs WHERE source_url = ?",
                {dateOnlyUrl});

        ASSERT_EQ(clearedStored.rowCount(), 1U);
        EXPECT_FALSE(clearedStored.value(0, 0).has_value());
        EXPECT_FALSE(clearedStored.value(0, 1).has_value());
    }

} // namespace jobpilot::service
