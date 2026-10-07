#include "jobpilot/service/application/application_dao.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "jobpilot/database/mysql_pool.h"
#include "jobpilot/service/user/user_dao.h"
#include "test_job_support.h"
#include "test_resume_support.h"
#include "test_user_support.h"

namespace jobpilot::service
{
    class ApplicationDaoTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            pool = jobpilot::test::MakeConnectionPool();
            userDao =
                std::make_unique<UserDao>(*pool);
            applicationDao =
                std::make_unique<ApplicationDao>(*pool);
            email =
                jobpilot::test::UniqueEmail(
                    "application-dao");

            const std::optional<std::uint64_t> userId =
                userDao->CreateUser(
                    "application-dao-user",
                    email,
                    "hash");

            ASSERT_TRUE(userId.has_value());
            this->userId = userId.value();
        }

        void TearDown() override
        {
            for (std::uint64_t jobId : jobIds)
            {
                jobpilot::test::CleanupJob(
                    pool.get(),
                    jobId);
            }

            jobpilot::test::CleanupUser(
                pool.get(),
                email);
        }

        std::uint64_t AddJob(
            const std::string &title)
        {
            const std::uint64_t jobId =
                jobpilot::test::InsertJob(
                    pool.get(),
                    "Acme",
                    title,
                    "Shenzhen",
                    "description",
                    jobpilot::test::UniqueJobToken(
                        "application-job"),
                    1,
                    "2026-10-01 10:00:00");
            jobIds.push_back(jobId);
            return jobId;
        }

        std::unique_ptr<
            jobpilot::core::ConnectionPool>
            pool;
        std::unique_ptr<UserDao> userDao;
        std::unique_ptr<ApplicationDao>
            applicationDao;
        std::uint64_t userId = 0;
        std::string email;
        std::vector<std::uint64_t> jobIds;
    };

    TEST_F(ApplicationDaoTest, CreatesAndRejectsDuplicate)
    {
        const std::uint64_t jobId =
            AddJob("Application Job");
        const jobpilot::test::ResumeVersionFixture resume =
            jobpilot::test::InsertResumeVersion(
                pool.get(),
                userId,
                "Application Resume",
                "content");

        std::uint64_t applicationId = 0;

        {
            auto transaction =
                pool->BeginTransaction();
            const ApplicationCreateResult created =
                applicationDao->Create(
                    transaction,
                    userId,
                    jobId,
                    resume.versionId);

            EXPECT_EQ(
                created.status,
                ApplicationCreateStatus::kCreated);
            applicationId = created.applicationId;
            transaction.Commit();
        }

        {
            auto transaction =
                pool->BeginTransaction();
            const ApplicationCreateResult duplicate =
                applicationDao->Create(
                    transaction,
                    userId,
                    jobId,
                    resume.versionId);

            EXPECT_EQ(
                duplicate.status,
                ApplicationCreateStatus::kDuplicate);
            EXPECT_EQ(
                duplicate.applicationId,
                applicationId);
            transaction.Rollback();
        }
    }

    TEST_F(ApplicationDaoTest, ListsFiltersStatisticsAndUpdatesStatus)
    {
        const jobpilot::test::ResumeVersionFixture resume =
            jobpilot::test::InsertResumeVersion(
                pool.get(),
                userId,
                "Application Resume",
                "content");

        std::vector<std::uint64_t> applicationIds;

        for (int index = 0; index < 4; ++index)
        {
            const std::uint64_t jobId =
                AddJob(
                    "Application Job " +
                    std::to_string(index));
            auto transaction =
                pool->BeginTransaction();
            const ApplicationCreateResult created =
                applicationDao->Create(
                    transaction,
                    userId,
                    jobId,
                    resume.versionId);

            ASSERT_EQ(
                created.status,
                ApplicationCreateStatus::kCreated);
            applicationIds.push_back(created.applicationId);
            transaction.Commit();
        }

        pool->Execute(
            "UPDATE applications SET status = ? WHERE id = ?",
            {std::int64_t(3), applicationIds[1]});
        pool->Execute(
            "UPDATE applications SET status = ? WHERE id = ?",
            {std::int64_t(6), applicationIds[2]});
        pool->Execute(
            "UPDATE applications SET status = ? WHERE id = ?",
            {std::int64_t(7), applicationIds[3]});

        const std::vector<ApplicationListItem> all =
            applicationDao->List(
                userId,
                std::nullopt,
                20,
                0);
        ASSERT_EQ(all.size(), 4U);
        EXPECT_EQ(
            applicationDao->Count(
                userId,
                std::nullopt),
            4U);

        const std::optional<std::int32_t> status3 = 3;
        const std::vector<ApplicationListItem> filtered =
            applicationDao->List(userId, status3, 20, 0);
        ASSERT_EQ(filtered.size(), 1U);
        EXPECT_EQ(filtered.front().id, applicationIds[1]);

        const ApplicationStatistics statistics =
            applicationDao->Statistics(userId);
        EXPECT_EQ(statistics.total, 4U);
        EXPECT_EQ(statistics.interview, 1U);
        EXPECT_EQ(statistics.offer, 1U);
        EXPECT_EQ(statistics.rejected, 1U);

        {
            auto transaction =
                pool->BeginTransaction();
            const std::optional<ApplicationRecord> application =
                applicationDao->FindOwnedForUpdate(
                    transaction,
                    userId,
                    applicationIds[0]);

            ASSERT_TRUE(application.has_value());
            EXPECT_TRUE(
                applicationDao->UpdateStatus(
                    transaction,
                    userId,
                    applicationIds[0],
                    2,
                    true));
            transaction.Commit();
        }
    }
} // namespace jobpilot::service
