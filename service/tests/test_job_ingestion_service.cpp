#include "jobpilot/service/job/job_ingestion_service.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "jobpilot/database/mysql_pool.h"
#include "jobpilot/service/job/job_dao.h"
#include "jobpilot/service/job/job_ingestion_dao.h"
#include "test_job_support.h"
#include "test_user_support.h"

namespace jobpilot::service
{
    namespace
    {
        class RecordingJobIngestionDao final
            : public IJobIngestionDao
        {
        public:
            explicit RecordingJobIngestionDao(
                JobIngestionDao *delegate = nullptr)
                : delegate_(delegate)
            {
            }

            JobUpsertResult UpsertJob(
                jobpilot::core::ConnectionPool::Transaction &transaction,
                const JobImportData &job) override
            {
                transactions.push_back(&transaction);
                ++upsertCalls;

                if (throwOnSecondUpsert &&
                    upsertCalls == 2)
                {
                    throw std::runtime_error(
                        "forced second upsert failure");
                }

                if (delegate_ != nullptr)
                {
                    return delegate_->UpsertJob(
                        transaction,
                        job);
                }

                return JobUpsertResult{
                    JobUpsertStatus::kCreated,
                    nextJobId++};
            }

            void ReplaceRequirements(
                jobpilot::core::ConnectionPool::Transaction &transaction,
                std::uint64_t jobId,
                const std::vector<JobRequirementImportData> &requirements)
                override
            {
                transactions.push_back(&transaction);
                ++replaceCalls;

                if (delegate_ != nullptr)
                {
                    delegate_->ReplaceRequirements(
                        transaction,
                        jobId,
                        requirements);
                }
            }

            std::vector<
                jobpilot::core::ConnectionPool::Transaction *>
                transactions;
            std::size_t upsertCalls = 0;
            std::size_t replaceCalls = 0;
            bool throwOnSecondUpsert = false;

        private:
            JobIngestionDao *delegate_;
            std::uint64_t nextJobId = 1;
        };
    } // namespace

    class JobIngestionServiceTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            pool = jobpilot::test::MakeConnectionPool();
        }

        void TearDown() override
        {
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
        }

        JobImportData MakeJob(
            const std::string &sourceUrl,
            const std::string &title = "Imported Job")
        {
            JobImportData job;
            job.company = "Import Company";
            job.title = title;
            job.location = "Shenzhen";
            job.salaryMin = 20;
            job.salaryMax = 35;
            job.salaryCurrency = "CNY";
            job.description = "Imported description";
            job.source = "import";
            job.sourceUrl = sourceUrl;
            return job;
        }

        JobIngestionItem MakeItem(
            const std::string &sourceUrl,
            const std::vector<JobRequirementImportData> &requirements = {})
        {
            sourceUrls.push_back(sourceUrl);
            return JobIngestionItem{
                MakeJob(sourceUrl),
                requirements};
        }

        std::unique_ptr<
            jobpilot::core::ConnectionPool>
            pool;
        std::vector<std::string> sourceUrls;
    };

    TEST_F(JobIngestionServiceTest, RejectsInvalidBatches)
    {
        RecordingJobIngestionDao dao;
        JobIngestionService service(dao, *pool);

        EXPECT_EQ(
            service.Import({}).code,
            ErrorCode::kInvalidParam);

        std::vector<JobIngestionItem> tooMany;

        for (int index = 0; index < 101; ++index)
        {
            tooMany.push_back(
                MakeItem(
                    jobpilot::test::UniqueJobToken(
                        "too-many")));
        }

        EXPECT_EQ(
            service.Import(tooMany).code,
            ErrorCode::kInvalidParam);

        const std::string duplicateUrl =
            jobpilot::test::UniqueJobToken(
                "duplicate-source");
        std::vector<JobIngestionItem> duplicateBatch = {
            MakeItem(duplicateUrl),
            MakeItem(duplicateUrl)};

        EXPECT_EQ(
            service.Import(duplicateBatch).code,
            ErrorCode::kInvalidParam);

        JobIngestionItem emptySource;
        emptySource.job = MakeJob("");
        EXPECT_EQ(
            service.Import({emptySource}).code,
            ErrorCode::kInvalidParam);

        JobIngestionItem invalidSalary =
            MakeItem(
                jobpilot::test::UniqueJobToken(
                    "invalid-salary"));
        invalidSalary.job.salaryMin = 40;
        invalidSalary.job.salaryMax = 20;
        EXPECT_EQ(
            service.Import({invalidSalary}).code,
            ErrorCode::kInvalidParam);

        JobIngestionItem invalidLevel =
            MakeItem(
                jobpilot::test::UniqueJobToken(
                    "invalid-level"),
                {{"C++", 4}});
        EXPECT_EQ(
            service.Import({invalidLevel}).code,
            ErrorCode::kInvalidParam);

        EXPECT_EQ(dao.upsertCalls, 0U);
        EXPECT_EQ(dao.replaceCalls, 0U);
    }

    TEST_F(JobIngestionServiceTest, CreatesUpdatesAndReplacesRequirements)
    {
        JobIngestionDao ingestionDao;
        RecordingJobIngestionDao dao(&ingestionDao);
        JobIngestionService service(dao, *pool);

        const std::string sourceUrl =
            jobpilot::test::UniqueJobToken(
                "service-idempotent");
        sourceUrls.push_back(sourceUrl);

        const ServiceResult<JobIngestionResult> created =
            service.Import(
                {JobIngestionItem{
                    MakeJob(sourceUrl, "Before"),
                    {{"C++", 3},
                     {"MySQL", 2},
                     {"Redis", 2}}}});

        ASSERT_EQ(created.code, ErrorCode::kSuccess);
        EXPECT_EQ(created.value.created, 1U);
        EXPECT_EQ(created.value.updated, 0U);
        EXPECT_EQ(created.value.total, 1U);

        const ServiceResult<JobIngestionResult> updated =
            service.Import(
                {JobIngestionItem{
                    MakeJob(sourceUrl, "After"),
                    {{"C++", 3},
                     {"PostgreSQL", 2}}}});

        ASSERT_EQ(updated.code, ErrorCode::kSuccess);
        EXPECT_EQ(updated.value.created, 0U);
        EXPECT_EQ(updated.value.updated, 1U);
        EXPECT_EQ(updated.value.total, 1U);

        const jobpilot::core::Result job =
            pool->Query(
                "SELECT id, title FROM jobs WHERE source_url = ?",
                {sourceUrl});

        ASSERT_EQ(job.rowCount(), 1U);
        ASSERT_TRUE(job.value(0, 0).has_value());
        EXPECT_EQ(job.value(0, 1).value(), "After");

        const std::uint64_t jobId =
            static_cast<std::uint64_t>(
                std::stoull(
                    job.value(0, 0).value()));
        JobDao readDao(*pool);
        const std::vector<JobRequirement> requirements =
            readDao.ListRequirements(jobId);

        ASSERT_EQ(requirements.size(), 2U);
        EXPECT_EQ(requirements[0].skill, "C++");
        EXPECT_EQ(requirements[1].skill, "PostgreSQL");
    }

    TEST_F(JobIngestionServiceTest, UsesOneTransactionAndRollsBackBatch)
    {
        JobIngestionDao ingestionDao;
        RecordingJobIngestionDao dao(&ingestionDao);
        dao.throwOnSecondUpsert = true;
        JobIngestionService service(dao, *pool);

        const std::string firstUrl =
            jobpilot::test::UniqueJobToken(
                "rollback-first");
        const std::string secondUrl =
            jobpilot::test::UniqueJobToken(
                "rollback-second");
        sourceUrls.push_back(firstUrl);
        sourceUrls.push_back(secondUrl);

        const ServiceResult<JobIngestionResult> result =
            service.Import(
                {JobIngestionItem{
                     MakeJob(firstUrl, "First"),
                     {{"C++", 3}}},
                 JobIngestionItem{
                     MakeJob(secondUrl, "Second"),
                     {{"Redis", 2}}}});

        EXPECT_EQ(
            result.code,
            ErrorCode::kDatabaseError);
        ASSERT_GE(dao.transactions.size(), 2U);

        for (const auto *transaction :
             dao.transactions)
        {
            EXPECT_EQ(
                transaction,
                dao.transactions.front());
        }

        for (const std::string &sourceUrl :
             {firstUrl, secondUrl})
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

} // namespace jobpilot::service
