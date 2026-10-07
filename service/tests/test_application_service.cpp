#include "jobpilot/service/application/application_service.h"

#include <cstdint>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "jobpilot/database/mysql_pool.h"
#include "jobpilot/service/application/application_dao.h"
#include "jobpilot/service/job/job_dao.h"
#include "jobpilot/service/resume/resume_dao.h"
#include "jobpilot/service/user/user_dao.h"
#include "test_job_support.h"
#include "test_resume_support.h"
#include "test_user_support.h"

namespace jobpilot::service
{
    namespace
    {
        class FakeAuthenticator final
            : public ISessionAuthenticator
        {
        public:
            ServiceResult<std::uint64_t> Authenticate(
                const std::string &token) override
            {
                if (token == "valid")
                {
                    return ServiceResult<std::uint64_t>{
                        ErrorCode::kSuccess,
                        1};
                }

                return ServiceResult<std::uint64_t>{
                    ErrorCode::kUnauthorized,
                    0};
            }
        };
    } // namespace

    class ApplicationServiceTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            pool = jobpilot::test::MakeConnectionPool();
            userDao =
                std::make_unique<UserDao>(*pool);
            jobDao =
                std::make_unique<JobDao>(*pool);
            resumeDao =
                std::make_unique<ResumeDao>(*pool);
            applicationDao =
                std::make_unique<ApplicationDao>(*pool);
            authenticator =
                std::make_unique<FakeAuthenticator>();
            service =
                std::make_unique<ApplicationService>(
                    *applicationDao,
                    *jobDao,
                    *resumeDao,
                    *pool,
                    *authenticator);

            userId = CreateUser("application-service");
            ASSERT_GT(userId, 0U);
        }

        void TearDown() override
        {
            for (std::uint64_t jobId : jobIds)
            {
                jobpilot::test::CleanupJob(
                    pool.get(),
                    jobId);
            }

            for (const std::string &email : emails)
            {
                jobpilot::test::CleanupUser(
                    pool.get(),
                    email);
            }
        }

        std::uint64_t CreateUser(
            const std::string &prefix)
        {
            const std::string email =
                jobpilot::test::UniqueEmail(prefix);
            emails.push_back(email);

            const std::optional<std::uint64_t> id =
                userDao->CreateUser(
                    prefix,
                    email,
                    "hash");

            if (!id.has_value())
            {
                throw std::runtime_error(
                    "failed to create user fixture");
            }

            return id.value();
        }

        std::uint64_t AddJob(
            std::int32_t status,
            const std::string &title)
        {
            const std::uint64_t id =
                jobpilot::test::InsertJob(
                    pool.get(),
                    "Acme",
                    title,
                    "Shenzhen",
                    "description",
                    jobpilot::test::UniqueJobToken(
                        "application-service-job"),
                    status,
                    "2026-10-01 10:00:00");
            jobIds.push_back(id);
            return id;
        }

        jobpilot::test::ResumeVersionFixture AddResume(
            std::uint64_t ownerUserId)
        {
            return jobpilot::test::InsertResumeVersion(
                pool.get(),
                ownerUserId,
                "Application Resume",
                "content");
        }

        std::unique_ptr<
            jobpilot::core::ConnectionPool>
            pool;
        std::unique_ptr<UserDao> userDao;
        std::unique_ptr<JobDao> jobDao;
        std::unique_ptr<ResumeDao> resumeDao;
        std::unique_ptr<ApplicationDao>
            applicationDao;
        std::unique_ptr<FakeAuthenticator>
            authenticator;
        std::unique_ptr<ApplicationService> service;
        std::uint64_t userId = 0;
        std::vector<std::string> emails;
        std::vector<std::uint64_t> jobIds;
    };

    TEST_F(ApplicationServiceTest, ValidatesRelationshipsAndDuplicate)
    {
        const std::uint64_t activeJob =
            AddJob(1, "Active Job");
        const jobpilot::test::ResumeVersionFixture resume =
            AddResume(userId);

        const ServiceResult<ApplicationCreateData> created =
            service->Create(
                userId,
                activeJob,
                resume.versionId);

        ASSERT_EQ(created.code, ErrorCode::kSuccess);
        EXPECT_EQ(created.value.status, 0);

        EXPECT_EQ(
            service->Create(
                       userId,
                       activeJob,
                       resume.versionId)
                .code,
            ErrorCode::kDuplicateApplication);

        EXPECT_EQ(
            service->Create(
                       userId,
                       999999999ULL,
                       resume.versionId)
                .code,
            ErrorCode::kJobNotFound);

        const std::uint64_t inactiveJob =
            AddJob(0, "Inactive Job");

        EXPECT_EQ(
            service->Create(
                       userId,
                       inactiveJob,
                       resume.versionId)
                .code,
            ErrorCode::kJobNotFound);

        EXPECT_EQ(
            service->Create(
                       userId,
                       activeJob + 1000000ULL,
                       resume.versionId)
                .code,
            ErrorCode::kJobNotFound);

        const std::uint64_t secondUser =
            CreateUser("application-service-second");
        const jobpilot::test::ResumeVersionFixture otherResume =
            AddResume(secondUser);

        EXPECT_EQ(
            service->Create(
                       userId,
                       AddJob(1, "Other Version Job"),
                       otherResume.versionId)
                .code,
            ErrorCode::kResumeNotFound);
    }

    TEST_F(ApplicationServiceTest, EnforcesStateMachineAndApplyTime)
    {
        const std::uint64_t jobId =
            AddJob(1, "State Machine Job");
        const jobpilot::test::ResumeVersionFixture resume =
            AddResume(userId);

        const ServiceResult<ApplicationCreateData> created =
            service->Create(
                userId,
                jobId,
                resume.versionId);

        ASSERT_EQ(created.code, ErrorCode::kSuccess);

        EXPECT_EQ(
            service->UpdateStatus(
                       userId,
                       created.value.applicationId,
                       2)
                .code,
            ErrorCode::kInvalidStatusTransition);

        EXPECT_EQ(
            service->UpdateStatus(
                       userId,
                       created.value.applicationId,
                       1)
                .code,
            ErrorCode::kSuccess);
        EXPECT_EQ(
            service->UpdateStatus(
                       userId,
                       created.value.applicationId,
                       2)
                .code,
            ErrorCode::kSuccess);

        {
            auto transaction =
                pool->BeginTransaction();
            const std::optional<ApplicationRecord> stored =
                applicationDao->FindOwnedForUpdate(
                    transaction,
                    userId,
                    created.value.applicationId);

            ASSERT_TRUE(stored.has_value());
            EXPECT_TRUE(stored->applyTime.has_value());
            transaction.Rollback();
        }

        EXPECT_EQ(
            service->UpdateStatus(
                       userId,
                       created.value.applicationId,
                       2)
                .code,
            ErrorCode::kInvalidStatusTransition);
        EXPECT_EQ(
            service->UpdateStatus(
                       userId,
                       created.value.applicationId,
                       4)
                .code,
            ErrorCode::kInvalidStatusTransition);

        EXPECT_EQ(
            service->UpdateStatus(
                       userId,
                       999999999ULL,
                       0)
                .code,
            ErrorCode::kApplicationNotFound);
    }

    TEST_F(ApplicationServiceTest, ListsAndAggregatesOwnedApplications)
    {
        const jobpilot::test::ResumeVersionFixture resume =
            AddResume(userId);
        std::vector<std::uint64_t> applicationIds;

        for (int i = 0; i < 3; ++i)
        {
            const ServiceResult<ApplicationCreateData> created =
                service->Create(
                    userId,
                    AddJob(
                        1,
                        "List Job " + std::to_string(i)),
                    resume.versionId);
            ASSERT_EQ(created.code, ErrorCode::kSuccess);
            applicationIds.push_back(
                created.value.applicationId);
        }

        pool->Execute(
            "UPDATE applications SET status = ? WHERE id = ?",
            {std::int64_t(3), applicationIds[1]});
        pool->Execute(
            "UPDATE applications SET status = ? WHERE id = ?",
            {std::int64_t(6), applicationIds[2]});

        const ServiceResult<ApplicationPage> list =
            service->List(
                userId,
                std::nullopt,
                1,
                20);

        ASSERT_EQ(list.code, ErrorCode::kSuccess);
        EXPECT_EQ(list.value.total, 3U);
        EXPECT_EQ(list.value.list.size(), 3U);

        const ServiceResult<ApplicationStatistics> statistics =
            service->Statistics(userId);

        ASSERT_EQ(statistics.code, ErrorCode::kSuccess);
        EXPECT_EQ(statistics.value.total, 3U);
        EXPECT_EQ(statistics.value.interview, 1U);
        EXPECT_EQ(statistics.value.offer, 1U);
    }

    TEST_F(ApplicationServiceTest, ConcurrentCreationProducesOneDuplicate)
    {
        const std::uint64_t jobId =
            AddJob(1, "Concurrent Job");
        const jobpilot::test::ResumeVersionFixture resume =
            AddResume(userId);

        std::mutex mutex;
        std::condition_variable condition;
        bool start = false;
        std::vector<ErrorCode> results;

        const auto create =
            [this,
             jobId,
             resumeVersionId = resume.versionId,
             &mutex,
             &condition,
             &start,
             &results]()
            {
                {
                    std::unique_lock<std::mutex> lock(
                        mutex);
                    condition.wait(
                        lock,
                        [&start]()
                        {
                            return start;
                        });
                }

                const ErrorCode code =
                    service->Create(
                               userId,
                               jobId,
                               resumeVersionId)
                        .code;

                std::lock_guard<std::mutex> lock(
                    mutex);
                results.push_back(code);
            };

        std::thread first(create);
        std::thread second(create);

        {
            std::lock_guard<std::mutex> lock(
                mutex);
            start = true;
        }

        condition.notify_all();
        first.join();
        second.join();

        ASSERT_EQ(results.size(), 2U);

        std::size_t success = 0;
        std::size_t duplicate = 0;

        for (ErrorCode code : results)
        {
            success +=
                code == ErrorCode::kSuccess ? 1U : 0U;
            duplicate +=
                code == ErrorCode::kDuplicateApplication
                    ? 1U
                    : 0U;
        }

        EXPECT_EQ(success, 1U);
        EXPECT_EQ(duplicate, 1U);
    }
} // namespace jobpilot::service
