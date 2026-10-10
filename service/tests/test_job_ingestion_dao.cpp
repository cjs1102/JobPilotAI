#include "jobpilot/service/job/job_ingestion_dao.h"

#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "jobpilot/database/mysql_pool.h"
#include "jobpilot/service/job/job_dao.h"
#include "test_job_support.h"
#include "test_user_support.h"

namespace jobpilot::service
{
    class JobIngestionDaoTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            pool = jobpilot::test::MakeConnectionPool();
            ingestionDao =
                std::make_unique<JobIngestionDao>();
            readDao =
                std::make_unique<JobDao>(*pool);
            sourceUrl =
                jobpilot::test::UniqueJobToken(
                    "job-ingestion-dao");
        }

        void TearDown() override
        {
            const jobpilot::core::Result jobs =
                pool->Query(
                    "SELECT id FROM jobs WHERE source_url = ?",
                    {sourceUrl});

            for (std::size_t row = 0;
                 row < jobs.rowCount();
                 ++row)
            {
                const jobpilot::core::Result::Value &value =
                    jobs.value(row, 0);

                if (value.has_value())
                {
                    jobpilot::test::CleanupJob(
                        pool.get(),
                        static_cast<std::uint64_t>(
                            std::stoull(value.value())));
                }
            }
        }

        JobImportData MakeJob(
            const std::string &title,
            const std::optional<std::string> &location =
                std::optional<std::string>(
                    "Shenzhen"))
        {
            JobImportData job;
            job.company = "Import Company";
            job.title = title;
            job.location = location;
            job.salaryMin = 20;
            job.salaryMax = 35;
            job.salaryCurrency = "CNY";
            job.description = "Imported job description";
            job.source = "import";
            job.sourceUrl = sourceUrl;
            return job;
        }

        std::unique_ptr<
            jobpilot::core::ConnectionPool>
            pool;
        std::unique_ptr<JobIngestionDao>
            ingestionDao;
        std::unique_ptr<JobDao> readDao;
        std::string sourceUrl;
    };

    TEST_F(JobIngestionDaoTest, CreatesJob)
    {
        std::uint64_t jobId = 0;

        {
            JobImportData createdJob = MakeJob("Created Job");
            // 0017A: the manual import path now carries the optional publication
            // instant into jobs.published_at.
            createdJob.publishedAt = "2026-09-20 00:00:00";

            auto transaction =
                pool->BeginTransaction();
            const JobUpsertResult result =
                ingestionDao->UpsertJob(
                    transaction,
                    createdJob);

            EXPECT_EQ(
                result.status,
                JobUpsertStatus::kCreated);
            EXPECT_GT(result.jobId, 0U);
            jobId = result.jobId;
            transaction.Commit();
        }

        const std::optional<JobDetail> job =
            readDao->FindActiveJobById(jobId);

        ASSERT_TRUE(job.has_value());
        EXPECT_EQ(job->summary.company, "Import Company");
        EXPECT_EQ(job->summary.title, "Created Job");
        EXPECT_EQ(
            job->summary.publishedAt,
            std::optional<std::string>("2026-09-20 00:00:00"));
    }

    TEST_F(JobIngestionDaoTest, UpdatesExistingJobByIdempotentSourceUrl)
    {
        std::uint64_t jobId = 0;

        {
            JobImportData createdJob = MakeJob("Before Update");
            // 0017A: stored on insert; the next upsert omits it on purpose.
            createdJob.publishedAt = "2026-09-20 00:00:00";

            auto transaction =
                pool->BeginTransaction();
            const JobUpsertResult created =
                ingestionDao->UpsertJob(
                    transaction,
                    createdJob);

            ASSERT_EQ(
                created.status,
                JobUpsertStatus::kCreated);
            jobId = created.jobId;
            transaction.Commit();
        }

        {
            JobImportData updatedJob =
                MakeJob("After Update");
            updatedJob.company = "Updated Company";
            updatedJob.location = "Shanghai";

            auto transaction =
                pool->BeginTransaction();
            const JobUpsertResult updated =
                ingestionDao->UpsertJob(
                    transaction,
                    updatedJob);

            EXPECT_EQ(
                updated.status,
                JobUpsertStatus::kUpdated);
            EXPECT_EQ(updated.jobId, jobId);
            transaction.Commit();
        }

        {
            // 0017A: publishedAt was omitted by the update above, so COALESCE
            // must have preserved the value written by the insert.
            const std::optional<JobDetail> preserved =
                readDao->FindActiveJobById(jobId);

            ASSERT_TRUE(preserved.has_value());
            EXPECT_EQ(
                preserved->summary.publishedAt,
                std::optional<std::string>("2026-09-20 00:00:00"));

            // ... and a payload that does carry it overwrites the stored value.
            JobImportData republished =
                MakeJob("After Update");
            republished.company = "Updated Company";
            republished.location = "Shanghai";
            republished.publishedAt = "2026-10-01 12:30:00";

            auto transaction =
                pool->BeginTransaction();
            const JobUpsertResult result =
                ingestionDao->UpsertJob(
                    transaction,
                    republished);

            EXPECT_EQ(
                result.status,
                JobUpsertStatus::kUpdated);
            EXPECT_EQ(result.jobId, jobId);
            transaction.Commit();
        }

        {
            JobImportData unchangedJob =
                MakeJob("After Update");
            unchangedJob.company = "Updated Company";
            unchangedJob.location = "Shanghai";

            auto transaction =
                pool->BeginTransaction();
            const JobUpsertResult unchanged =
                ingestionDao->UpsertJob(
                    transaction,
                    unchangedJob);

            EXPECT_EQ(
                unchanged.status,
                JobUpsertStatus::kUpdated);
            EXPECT_EQ(unchanged.jobId, jobId);
            transaction.Rollback();
        }

        const std::optional<JobDetail> job =
            readDao->FindActiveJobById(jobId);
        const jobpilot::core::Result count =
            pool->Query(
                "SELECT COUNT(*) FROM jobs WHERE source_url = ?",
                {sourceUrl});

        ASSERT_TRUE(job.has_value());
        ASSERT_EQ(count.rowCount(), 1U);
        ASSERT_TRUE(count.value(0, 0).has_value());
        EXPECT_EQ(count.value(0, 0).value(), "1");
        EXPECT_EQ(job->summary.company, "Updated Company");
        EXPECT_EQ(job->summary.title, "After Update");
        EXPECT_EQ(
            job->summary.location,
            std::optional<std::string>("Shanghai"));
        EXPECT_EQ(
            job->summary.publishedAt,
            std::optional<std::string>("2026-10-01 12:30:00"));
    }

    TEST_F(JobIngestionDaoTest, ReplacesRequirementsAsSet)
    {
        std::uint64_t jobId = 0;

        {
            auto transaction =
                pool->BeginTransaction();
            const JobUpsertResult created =
                ingestionDao->UpsertJob(
                    transaction,
                    MakeJob("Requirements Job"));
            ASSERT_EQ(
                created.status,
                JobUpsertStatus::kCreated);
            jobId = created.jobId;

            ingestionDao->ReplaceRequirements(
                transaction,
                jobId,
                {
                    {"C++", 3},
                    {"MySQL", 2},
                    {"Redis", 2},
                });
            transaction.Commit();
        }

        {
            auto transaction =
                pool->BeginTransaction();
            const JobUpsertResult updated =
                ingestionDao->UpsertJob(
                    transaction,
                    MakeJob("Requirements Job"));
            EXPECT_EQ(
                updated.status,
                JobUpsertStatus::kUpdated);

            ingestionDao->ReplaceRequirements(
                transaction,
                jobId,
                {
                    {"C++", 3},
                    {"PostgreSQL", 2},
                });
            transaction.Commit();
        }

        const std::vector<JobRequirement> requirements =
            readDao->ListRequirements(jobId);

        ASSERT_EQ(requirements.size(), 2U);
        EXPECT_EQ(requirements[0].skill, "C++");
        EXPECT_EQ(requirements[1].skill, "PostgreSQL");

        for (const JobRequirement &requirement :
             requirements)
        {
            EXPECT_NE(requirement.skill, "MySQL");
            EXPECT_NE(requirement.skill, "Redis");
        }
    }

    TEST_F(JobIngestionDaoTest, EmptyRequirementsDeletesAll)
    {
        std::uint64_t jobId = 0;

        {
            auto transaction =
                pool->BeginTransaction();
            const JobUpsertResult created =
                ingestionDao->UpsertJob(
                    transaction,
                    MakeJob("Empty Requirements Job"));
            ASSERT_EQ(
                created.status,
                JobUpsertStatus::kCreated);
            jobId = created.jobId;
            ingestionDao->ReplaceRequirements(
                transaction,
                jobId,
                {
                    {"C++", 3},
                    {"MySQL", 2},
                });
            transaction.Commit();
        }

        {
            auto transaction =
                pool->BeginTransaction();
            ingestionDao->ReplaceRequirements(
                transaction,
                jobId,
                {});
            transaction.Commit();
        }

        EXPECT_TRUE(
            readDao->ListRequirements(jobId).empty());
    }

    TEST_F(JobIngestionDaoTest, ObeysExternalTransactionRollback)
    {
        std::uint64_t jobId = 0;

        {
            auto transaction =
                pool->BeginTransaction();
            const JobUpsertResult created =
                ingestionDao->UpsertJob(
                    transaction,
                    MakeJob("Rollback Before"));
            ASSERT_EQ(
                created.status,
                JobUpsertStatus::kCreated);
            jobId = created.jobId;
            ingestionDao->ReplaceRequirements(
                transaction,
                jobId,
                {{"C++", 3}});
            transaction.Commit();
        }

        {
            auto transaction =
                pool->BeginTransaction();
            const JobUpsertResult updated =
                ingestionDao->UpsertJob(
                    transaction,
                    MakeJob("Rollback After"));
            EXPECT_EQ(
                updated.status,
                JobUpsertStatus::kUpdated);
            ingestionDao->ReplaceRequirements(
                transaction,
                jobId,
                {{"PostgreSQL", 2}});

            EXPECT_THROW(
                transaction.Execute(
                    "INSERT INTO missing_rollback_table "
                    "(id) VALUES (1)",
                    {}),
                std::exception);
            transaction.Rollback();
        }

        const std::optional<JobDetail> job =
            readDao->FindActiveJobById(jobId);

        ASSERT_TRUE(job.has_value());
        EXPECT_EQ(job->summary.title, "Rollback Before");

        const std::vector<JobRequirement> requirements =
            readDao->ListRequirements(jobId);

        ASSERT_EQ(requirements.size(), 1U);
        EXPECT_EQ(requirements[0].skill, "C++");
    }

} // namespace jobpilot::service
