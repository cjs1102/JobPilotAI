#include <arpa/inet.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/ostream_sink.h>

#include "jobpilot/cache/session_manager.h"
#include "jobpilot/core/logger.h"
#include "jobpilot/http/http_server.h"
#include "jobpilot/network/event_loop.h"
#include "jobpilot/service/auth/session_authenticator.h"
#include "jobpilot/service/job/internal_job_review_controller.h"
#include "jobpilot/service/job/job_controller.h"
#include "jobpilot/service/job/job_dao.h"
#include "jobpilot/service/job/job_review.h"
#include "jobpilot/service/job/job_review_dao.h"
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
        constexpr std::uint16_t kTestPort = 19091;

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
            const std::string &body = std::string(),
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
                std::stoi(rawResponse.substr(9, 3));
            response.body =
                rawResponse.substr(headerEnd + 4);

            try
            {
                response.jsonBody =
                    nlohmann::json::parse(response.body);
                response.hasJson = true;
            }
            catch (const nlohmann::json::parse_error &)
            {
                response.hasJson = false;
            }

            return response;
        }
    } // namespace

    class JobReviewHttpTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            pool = jobpilot::test::MakeConnectionPool();
            redis = jobpilot::test::MakeRedisClient();
            sessions =
                std::make_unique<
                    jobpilot::core::SessionManager>(*redis);
            userDao = std::make_unique<UserDao>(*pool);
            userService = std::make_unique<UserService>(
                *userDao,
                *sessions);
            userController = std::make_unique<UserController>(
                *userService);
            jobDao = std::make_unique<JobDao>(*pool);
            jobService = std::make_unique<JobService>(*jobDao);
            jobController = std::make_unique<JobController>(
                *jobService);

            reviewDao = std::make_unique<JobReviewDao>(*pool);
            reviewService = std::make_unique<JobReviewService>(
                *reviewDao,
                PlatformNameRegistry::FromNames({"牛客"}));

            internalToken =
                jobpilot::test::UniqueJobToken(
                    "review-internal-token");

            reviewController =
                std::make_unique<
                    InternalJobReviewController>(
                    *reviewService,
                    internalToken);

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
                            nullptr,
                            nullptr,
                            nullptr,
                            reviewController.get());

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

            std::unique_lock<std::mutex> lock(state.mutex);

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
                std::lock_guard<std::mutex> lock(state.mutex);
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

            // Defensive cleanup first so a failed assertion can never leak a
            // row, then clear any backfill-marked verdict this test created.
            for (std::uint64_t jobId : jobIds)
            {
                jobpilot::test::CleanupJob(pool.get(), jobId);
            }

            reviewDao->RevertBackfill();
        }

        // Inserts an active job; `reviewStatus` and a review finding are
        // written directly so the queue/review paths can be exercised without
        // going through ingest.
        std::uint64_t AddJob(
            const std::string &company,
            const std::string &title,
            const std::string &sourceUrl,
            std::int32_t reviewStatus = 0)
        {
            const std::uint64_t jobId =
                jobpilot::test::InsertJob(
                    pool.get(),
                    company,
                    title,
                    std::optional<std::string>("Shenzhen"),
                    std::optional<std::string>("Description"),
                    sourceUrl,
                    1,
                    "2026-10-01 10:00:00");

            jobIds.push_back(jobId);

            if (reviewStatus != 0)
            {
                pool->Execute(
                    "UPDATE jobs SET review_status = ? WHERE id = ?",
                    {static_cast<std::int64_t>(reviewStatus),
                     jobId});
            }

            return jobId;
        }

        void AddReviewItem(
            std::uint64_t jobId,
            const std::string &code,
            const std::string &text,
            const std::string &outcome = "needs_review",
            const std::string &resolution = "open",
            const std::optional<std::string> &resolvedBy =
                std::nullopt)
        {
            pool->Execute(
                "INSERT INTO job_review_items "
                "(job_id, reason_code, reason_text, outcome, resolution, "
                "resolved_by) VALUES (?, ?, ?, ?, ?, ?)",
                {jobId,
                 code,
                 text,
                 outcome,
                 resolution,
                 resolvedBy.has_value()
                     ? jobpilot::core::SqlParam(resolvedBy.value())
                     : jobpilot::core::SqlParam(nullptr)});
        }

        std::int32_t ReviewStatus(std::uint64_t jobId) const
        {
            const jobpilot::core::Result result =
                pool->Query(
                    "SELECT review_status FROM jobs WHERE id = ?",
                    {jobId});

            if (result.rowCount() == 0 ||
                !result.value(0, 0).has_value())
            {
                return -1;
            }

            return static_cast<std::int32_t>(
                std::stol(result.value(0, 0).value()));
        }

        // The public list endpoint itself, so visibility is asserted through
        // the real HTTP contract and not only through SQL.
        std::size_t PublicHits(const std::string &keyword)
        {
            const std::optional<HttpResponseData> response =
                SendRequest(
                    "GET",
                    "/api/v1/jobs?keyword=" + keyword);

            if (!response.has_value() ||
                !response->hasJson)
            {
                return 0;
            }

            return response->jsonBody["data"]["total"]
                .get<std::size_t>();
        }

        // Connected schema name: identity evidence for the scratch-database
        // guard on the apply/revert path.
        std::string ConnectedDatabase() const
        {
            const jobpilot::core::Result result =
                pool->Query("SELECT DATABASE()", {});

            if (result.rowCount() == 0 ||
                !result.value(0, 0).has_value())
            {
                return std::string();
            }

            return result.value(0, 0).value();
        }

        ServerState state;
        std::thread serverThread;
        std::unique_ptr<jobpilot::core::ConnectionPool> pool;
        std::unique_ptr<jobpilot::core::RedisClient> redis;
        std::unique_ptr<jobpilot::core::SessionManager> sessions;
        std::unique_ptr<UserDao> userDao;
        std::unique_ptr<UserService> userService;
        std::unique_ptr<UserController> userController;
        std::unique_ptr<JobDao> jobDao;
        std::unique_ptr<JobService> jobService;
        std::unique_ptr<JobController> jobController;
        std::unique_ptr<JobReviewDao> reviewDao;
        std::unique_ptr<JobReviewService> reviewService;
        std::unique_ptr<InternalJobReviewController>
            reviewController;
        std::string internalToken;
        std::vector<std::uint64_t> jobIds;
    };

    TEST_F(JobReviewHttpTest, RejectsMissingOrWrongToken)
    {
        const auto queue =
            SendRequest(
                "GET",
                "/internal/jobs/review-queue");

        ASSERT_TRUE(queue.has_value());
        ASSERT_TRUE(queue->hasJson);
        EXPECT_EQ(queue->statusCode, 401);
        EXPECT_EQ(queue->jsonBody["code"], 20001);

        const auto wrong =
            SendRequest(
                "GET",
                "/internal/jobs/review-queue",
                "",
                "wrong-token");

        ASSERT_TRUE(wrong.has_value());
        ASSERT_TRUE(wrong->hasJson);
        EXPECT_EQ(wrong->statusCode, 401);
        EXPECT_EQ(wrong->jsonBody["code"], 20001);

        const auto review =
            SendRequest(
                "POST",
                "/internal/jobs/review/1",
                R"({"decision": "approve", "reviewer": "ops"})");

        ASSERT_TRUE(review.has_value());
        ASSERT_TRUE(review->hasJson);
        EXPECT_EQ(review->statusCode, 401);

        const auto backfill =
            SendRequest(
                "POST",
                "/internal/jobs/review/backfill?mode=dry_run",
                "{}");

        ASSERT_TRUE(backfill.has_value());
        ASSERT_TRUE(backfill->hasJson);
        EXPECT_EQ(backfill->statusCode, 401);
    }

    TEST_F(JobReviewHttpTest, QueueListsReasonStatusAndIdentifiers)
    {
        const std::string token =
            jobpilot::test::UniqueJobToken("queue-job");
        const std::uint64_t jobId = AddJob(
            "Queue Co",
            token,
            "https://example.invalid/job/queue",
            1);
        AddReviewItem(
            jobId,
            "employer_is_platform",
            "employer equals a registered platform name",
            "needs_review");

        const auto response =
            SendRequest(
                "GET",
                "/internal/jobs/review-queue?status=needs_review",
                "",
                internalToken);

        ASSERT_TRUE(response.has_value());
        ASSERT_TRUE(response->hasJson);
        EXPECT_EQ(response->statusCode, 200);
        EXPECT_GE(response->jsonBody["total"].get<std::size_t>(), 1U);

        bool found = false;

        for (const auto &item : response->jsonBody["items"])
        {
            if (item["job_id"].get<std::uint64_t>() != jobId)
            {
                continue;
            }

            found = true;
            EXPECT_EQ(item["title"], token);
            EXPECT_EQ(item["company"], "Queue Co");
            EXPECT_EQ(item["review_status"], 1);
            EXPECT_EQ(
                item["reason_code"],
                "employer_is_platform");
            EXPECT_EQ(
                item["reason_text"],
                "employer equals a registered platform name");
            EXPECT_EQ(item["outcome"], "needs_review");
            EXPECT_EQ(item["resolution"], "open");
            EXPECT_EQ(item["source_url"],
                      "https://example.invalid/job/queue");
        }

        EXPECT_TRUE(found);

        const auto invalid =
            SendRequest(
                "GET",
                "/internal/jobs/review-queue?status=bogus",
                "",
                internalToken);

        ASSERT_TRUE(invalid.has_value());
        ASSERT_TRUE(invalid->hasJson);
        EXPECT_EQ(invalid->statusCode, 400);
    }

    TEST_F(JobReviewHttpTest, ApproveMakesTheRowVisibleAndIsIdempotent)
    {
        const std::string token =
            jobpilot::test::UniqueJobToken("approve-job");
        const std::uint64_t jobId = AddJob(
            "Approve Co",
            token,
            "https://example.invalid/job/approve",
            1);
        AddReviewItem(
            jobId,
            "source_url_not_detail",
            "source_url looks like a site root/list page",
            "needs_review");

        EXPECT_EQ(PublicHits(token), 0U);

        const auto approved =
            SendRequest(
                "POST",
                "/internal/jobs/review/" +
                    std::to_string(jobId),
                R"({"decision": "approve", "reviewer": "ops-lead", "note": "verified employer"})",
                internalToken);

        ASSERT_TRUE(approved.has_value());
        ASSERT_TRUE(approved->hasJson);
        EXPECT_EQ(approved->statusCode, 200);
        EXPECT_EQ(approved->jsonBody["review_status"], 0);
        EXPECT_EQ(approved->jsonBody["decision"], "approve");
        EXPECT_EQ(approved->jsonBody["idempotent"], false);

        EXPECT_EQ(ReviewStatus(jobId), 0);
        EXPECT_EQ(PublicHits(token), 1U);

        const jobpilot::core::Result audit = pool->Query(
            "SELECT resolution, resolved_by FROM job_review_items "
            "WHERE job_id = ? ORDER BY id DESC LIMIT 1",
            {jobId});

        ASSERT_EQ(audit.rowCount(), 1U);
        EXPECT_EQ(audit.value(0, 0).value(), "approved");
        EXPECT_EQ(audit.value(0, 1).value(), "ops-lead");

        // Repeating the identical decision is an idempotent 200 no-op.
        const auto repeated =
            SendRequest(
                "POST",
                "/internal/jobs/review/" +
                    std::to_string(jobId),
                R"({"decision": "approve", "reviewer": "ops-lead", "note": "verified employer"})",
                internalToken);

        ASSERT_TRUE(repeated.has_value());
        ASSERT_TRUE(repeated->hasJson);
        EXPECT_EQ(repeated->statusCode, 200);
        EXPECT_EQ(repeated->jsonBody["idempotent"], true);

        // A conflicting second decision is a 409.
        const auto conflict =
            SendRequest(
                "POST",
                "/internal/jobs/review/" +
                    std::to_string(jobId),
                R"({"decision": "reject", "reviewer": "ops-lead", "note": "changed my mind"})",
                internalToken);

        ASSERT_TRUE(conflict.has_value());
        ASSERT_TRUE(conflict->hasJson);
        EXPECT_EQ(conflict->statusCode, 409);
        EXPECT_EQ(conflict->jsonBody["code"], 40005);
    }

    TEST_F(JobReviewHttpTest, FixValidatesAgainstTheContractsAndAudits)
    {
        const std::string token =
            jobpilot::test::UniqueJobToken("fix-job");
        const std::uint64_t jobId = AddJob(
            "Fix Co",
            token,
            "https://example.invalid/job/fix",
            1);
        AddReviewItem(
            jobId,
            "employer_is_platform",
            "employer equals a registered platform name",
            "needs_review");

        // Invalid fix (non-http source_url) -> 400 + diagnostic, no change.
        const auto rejected =
            SendRequest(
                "POST",
                "/internal/jobs/review/" +
                    std::to_string(jobId),
                R"({"decision": "fix", "reviewer": "ops", "note": "repair", "fix": {"source_url": "not-a-url"}})",
                internalToken);

        ASSERT_TRUE(rejected.has_value());
        ASSERT_TRUE(rejected->hasJson);
        EXPECT_EQ(rejected->statusCode, 400);
        EXPECT_EQ(
            rejected->jsonBody["data"]["diagnostic"]["code"],
            "source_url_unusable");
        EXPECT_EQ(
            rejected->jsonBody["data"]["diagnostic"]["field"],
            "source_url");
        EXPECT_EQ(ReviewStatus(jobId), 1);

        // A platform name is not an acceptable employer either.
        const auto platform =
            SendRequest(
                "POST",
                "/internal/jobs/review/" +
                    std::to_string(jobId),
                R"({"decision": "fix", "reviewer": "ops", "note": "repair", "fix": {"company": "牛客网"}})",
                internalToken);

        ASSERT_TRUE(platform.has_value());
        ASSERT_TRUE(platform->hasJson);
        EXPECT_EQ(platform->statusCode, 400);
        EXPECT_EQ(
            platform->jsonBody["data"]["diagnostic"]["code"],
            "employer_is_platform");

        const auto fixed =
            SendRequest(
                "POST",
                "/internal/jobs/review/" +
                    std::to_string(jobId),
                R"({"decision": "fix", "reviewer": "ops", "note": "employer corrected", "fix": {"company": "Real Employer Ltd", "title": "FixedTitle"}})",
                internalToken);

        ASSERT_TRUE(fixed.has_value());
        ASSERT_TRUE(fixed->hasJson);
        EXPECT_EQ(fixed->statusCode, 200);
        EXPECT_EQ(fixed->jsonBody["review_status"], 0);
        ASSERT_EQ(
            fixed->jsonBody["changed_fields"].size(),
            2U);
        EXPECT_EQ(
            fixed->jsonBody["changed_fields"][0],
            "company");
        EXPECT_EQ(
            fixed->jsonBody["changed_fields"][1],
            "title");

        const jobpilot::core::Result row = pool->Query(
            "SELECT company, title, review_status, review_note "
            "FROM jobs WHERE id = ?",
            {jobId});

        ASSERT_EQ(row.rowCount(), 1U);
        EXPECT_EQ(row.value(0, 0).value(), "Real Employer Ltd");
        EXPECT_EQ(row.value(0, 1).value(), "FixedTitle");
        EXPECT_EQ(row.value(0, 2).value(), "0");
        EXPECT_EQ(row.value(0, 3).value(), "employer corrected");

        // Per-field before/after audit rows, resolved as 'fixed'.
        const jobpilot::core::Result audit = pool->Query(
            "SELECT reason_code, reason_text, resolution "
            "FROM job_review_items WHERE job_id = ? "
            "AND reason_code LIKE 'review_fix_%' "
            "ORDER BY id ASC",
            {jobId});

        ASSERT_EQ(audit.rowCount(), 2U);
        EXPECT_EQ(
            audit.value(0, 0).value(),
            "review_fix_company");
        EXPECT_EQ(
            audit.value(0, 1).value(),
            "company: Fix Co => Real Employer Ltd");
        EXPECT_EQ(audit.value(0, 2).value(), "fixed");
        EXPECT_EQ(
            audit.value(1, 1).value(),
            "title: " + token + " => FixedTitle");

        EXPECT_EQ(PublicHits("FixedTitle"), 1U);
    }

    TEST_F(JobReviewHttpTest, RejectRequiresANoteHidesAndRetainsTheRow)
    {
        const std::string token =
            jobpilot::test::UniqueJobToken("reject-job");
        const std::uint64_t jobId = AddJob(
            "Reject Co",
            token,
            "https://example.invalid/job/reject",
            1);
        AddReviewItem(
            jobId,
            "duplicate_content_in_batch",
            "duplicate content inside one payload",
            "needs_review");

        const auto noNote =
            SendRequest(
                "POST",
                "/internal/jobs/review/" +
                    std::to_string(jobId),
                R"({"decision": "reject", "reviewer": "ops"})",
                internalToken);

        ASSERT_TRUE(noNote.has_value());
        ASSERT_TRUE(noNote->hasJson);
        EXPECT_EQ(noNote->statusCode, 400);
        EXPECT_EQ(ReviewStatus(jobId), 1);

        const auto rejected =
            SendRequest(
                "POST",
                "/internal/jobs/review/" +
                    std::to_string(jobId),
                R"({"decision": "reject", "reviewer": "ops", "note": "spam posting"})",
                internalToken);

        ASSERT_TRUE(rejected.has_value());
        ASSERT_TRUE(rejected->hasJson);
        EXPECT_EQ(rejected->statusCode, 200);
        EXPECT_EQ(rejected->jsonBody["review_status"], 2);

        EXPECT_EQ(ReviewStatus(jobId), 2);
        EXPECT_EQ(PublicHits(token), 0U);

        // The row is retained for audit, never deleted.
        const jobpilot::core::Result row = pool->Query(
            "SELECT review_status, review_note FROM jobs WHERE id = ?",
            {jobId});

        ASSERT_EQ(row.rowCount(), 1U);
        EXPECT_EQ(row.value(0, 0).value(), "2");
        EXPECT_EQ(row.value(0, 1).value(), "spam posting");

        const jobpilot::core::Result audit = pool->Query(
            "SELECT resolution FROM job_review_items "
            "WHERE job_id = ? ORDER BY id DESC LIMIT 1",
            {jobId});

        ASSERT_EQ(audit.rowCount(), 1U);
        EXPECT_EQ(audit.value(0, 0).value(), "rejected");
    }

    TEST_F(JobReviewHttpTest, BackfillDryRunWritesNothingApplyAndRevertRestore)
    {
        // ITEM 4 (0015A): apply/revert may only ever run against an explicitly
        // named scratch database. This is the ONLY test in this binary that
        // flips existing rows, so it must never fall back to the shared dev
        // database (or any database implicitly chosen by ctest).
        //
        // Run it with:
        //   MYSQL_DATABASE=jobpilot_review_scratch \
        //   JOBPILOT_TEST_REVIEW_DB=jobpilot_review_scratch \
        //   MYSQL_PASSWORD=root ./build/presets/release/test_job_review_http
        // Without the variable the case skips explicitly; if it points at the
        // dev database the case fails loudly.
        const char *scratchEnv =
            std::getenv("JOBPILOT_TEST_REVIEW_DB");
        const std::string connected = ConnectedDatabase();

        std::cout << "REVIEW_DB_ISOLATION connected=" << connected
                  << " scratch_env="
                  << (scratchEnv == nullptr ? "<unset>" : scratchEnv)
                  << "\n";

        if (scratchEnv == nullptr || *scratchEnv == '\0')
        {
            GTEST_SKIP()
                << "JOBPILOT_TEST_REVIEW_DB is not set: refusing to run "
                   "backfill apply/revert against '"
                << connected
                << "' implicitly. Set JOBPILOT_TEST_REVIEW_DB and "
                   "MYSQL_DATABASE to a scratch database "
                   "(e.g. jobpilot_review_scratch, loaded from "
                   "database/schema/*.sql).";
        }

        const std::string scratch = scratchEnv;

        if (scratch == "jobpilot" || connected == "jobpilot")
        {
            GTEST_FAIL()
                << "refusing to run backfill apply/revert against the dev "
                   "database 'jobpilot' (connected='" << connected
                << "', requested='" << scratch << "')";
            return;
        }

        if (connected != scratch)
        {
            GTEST_FAIL()
                << "connected database '" << connected
                << "' is not the requested scratch database '" << scratch
                << "'; set MYSQL_DATABASE and JOBPILOT_TEST_REVIEW_DB to the "
                   "same scratch database";
            return;
        }

        const std::string token =
            jobpilot::test::UniqueJobToken("backfill-job");
        const std::uint64_t jobId = AddJob(
            "牛客",
            token,
            "https://example.invalid/job/backfill-" + token,
            0);

        const auto dryRun =
            SendRequest(
                "POST",
                "/internal/jobs/review/backfill?mode=dry_run",
                "{}",
                internalToken);

        ASSERT_TRUE(dryRun.has_value());
        ASSERT_TRUE(dryRun->hasJson);
        EXPECT_EQ(dryRun->statusCode, 200);
        EXPECT_EQ(dryRun->jsonBody["mode"], "dry_run");
        EXPECT_GE(
            dryRun->jsonBody["would_flag"].get<std::size_t>(),
            1U);
        EXPECT_GE(
            dryRun->jsonBody["per_reason"]["employer_is_platform"]
                .get<std::size_t>(),
            1U);
        EXPECT_EQ(
            dryRun->jsonBody["threshold_percent"],
            5);
        EXPECT_EQ(
            dryRun->jsonBody["threshold_rows"],
            100);

        // The flag must be consistent with the reported numbers (the shared
        // dev DB is small, so a single flagged row can legitimately trip the
        // 5% rule).
        const std::size_t activeJobs =
            dryRun->jsonBody["active_jobs"].get<std::size_t>();
        const std::size_t wouldFlag =
            dryRun->jsonBody["would_flag"].get<std::size_t>();

        EXPECT_EQ(
            dryRun->jsonBody["threshold_exceeded"].get<bool>(),
            wouldFlag > 100 ||
                (activeJobs > 0 &&
                 wouldFlag * 100 > 5 * activeJobs));

        // dry_run writes nothing.
        EXPECT_EQ(ReviewStatus(jobId), 0);
        EXPECT_EQ(dryRun->jsonBody["applied"], 0);
        EXPECT_EQ(dryRun->jsonBody["reverted"], 0);
        EXPECT_EQ(
            dryRun->jsonBody["before_needs_review"],
            dryRun->jsonBody["after_needs_review"]
                .get<std::size_t>() -
                dryRun->jsonBody["would_flag"].get<std::size_t>());

        const auto applied =
            SendRequest(
                "POST",
                "/internal/jobs/review/backfill?mode=apply",
                "{}",
                internalToken);

        ASSERT_TRUE(applied.has_value());
        ASSERT_TRUE(applied->hasJson);
        EXPECT_EQ(applied->statusCode, 200);
        EXPECT_GE(applied->jsonBody["applied"].get<std::size_t>(), 1U);
        EXPECT_EQ(ReviewStatus(jobId), 1);

        // Auditable before/after counts: after == before + applied.
        EXPECT_EQ(
            applied->jsonBody["after_needs_review"].get<std::size_t>(),
            applied->jsonBody["before_needs_review"]
                    .get<std::size_t>() +
                applied->jsonBody["applied"].get<std::size_t>());

        // The classified row is queued with the engine's reason.
        const jobpilot::core::Result queued = pool->Query(
            "SELECT reason_code, resolution, resolved_by "
            "FROM job_review_items WHERE job_id = ? "
            "ORDER BY id DESC LIMIT 1",
            {jobId});

        ASSERT_EQ(queued.rowCount(), 1U);
        EXPECT_EQ(
            queued.value(0, 0).value(),
            "employer_is_platform");
        EXPECT_EQ(queued.value(0, 1).value(), "open");
        EXPECT_EQ(queued.value(0, 2).value(), "backfill");

        const auto reverted =
            SendRequest(
                "POST",
                "/internal/jobs/review/backfill?mode=revert",
                "{}",
                internalToken);

        ASSERT_TRUE(reverted.has_value());
        ASSERT_TRUE(reverted->hasJson);
        EXPECT_EQ(reverted->statusCode, 200);
        EXPECT_GE(reverted->jsonBody["reverted"].get<std::size_t>(), 1U);
        EXPECT_EQ(ReviewStatus(jobId), 0);

        // Rollback basis: the needs_review distribution is restored to the
        // pre-apply count.
        EXPECT_EQ(
            reverted->jsonBody["after_needs_review"].get<std::size_t>(),
            dryRun->jsonBody["before_needs_review"].get<std::size_t>());

        const jobpilot::core::Result leftover = pool->Query(
            "SELECT COUNT(*) FROM job_review_items "
            "WHERE job_id = ? AND resolved_by = 'backfill'",
            {jobId});

        ASSERT_EQ(leftover.rowCount(), 1U);
        EXPECT_EQ(leftover.value(0, 0).value(), "0");

        const auto badMode =
            SendRequest(
                "POST",
                "/internal/jobs/review/backfill?mode=nope",
                "{}",
                internalToken);

        ASSERT_TRUE(badMode.has_value());
        ASSERT_TRUE(badMode->hasJson);
        EXPECT_EQ(badMode->statusCode, 400);
    }

    // The internal token must never reach any log output.
    TEST_F(JobReviewHttpTest, InternalTokenNeverAppearsInLogs)
    {
        std::ostringstream captured;

        jobpilot::core::Logger::Initialize(
            "trace",
            std::make_shared<
                spdlog::sinks::ostream_sink_mt>(captured));

        const std::string token =
            jobpilot::test::UniqueJobToken("logscan-job");
        const std::uint64_t jobId = AddJob(
            "Log Co",
            token,
            "https://example.invalid/job/logscan",
            1);
        AddReviewItem(
            jobId,
            "employer_is_platform",
            "employer equals a registered platform name",
            "needs_review");

        const auto queue =
            SendRequest(
                "GET",
                "/internal/jobs/review-queue",
                "",
                internalToken);
        ASSERT_TRUE(queue.has_value());
        EXPECT_EQ(queue->statusCode, 200);

        const auto review =
            SendRequest(
                "POST",
                "/internal/jobs/review/" +
                    std::to_string(jobId),
                R"({"decision": "approve", "reviewer": "ops"})",
                internalToken);
        ASSERT_TRUE(review.has_value());
        EXPECT_EQ(review->statusCode, 200);

        const auto backfill =
            SendRequest(
                "POST",
                "/internal/jobs/review/backfill?mode=dry_run",
                "{}",
                internalToken);
        ASSERT_TRUE(backfill.has_value());
        EXPECT_EQ(backfill->statusCode, 200);

        // A wrong token is rejected and still never logged.
        const auto wrong =
            SendRequest(
                "GET",
                "/internal/jobs/review-queue",
                "",
                "wrong-token-value");
        ASSERT_TRUE(wrong.has_value());
        EXPECT_EQ(wrong->statusCode, 401);

        jobpilot::core::Logger::Get()->flush();

        const std::string logs = captured.str();

        EXPECT_EQ(logs.find(internalToken), std::string::npos);
        EXPECT_EQ(
            logs.find("wrong-token-value"),
            std::string::npos);
        EXPECT_EQ(
            logs.find("X-Internal-Token"),
            std::string::npos);
    }

} // namespace jobpilot::service
