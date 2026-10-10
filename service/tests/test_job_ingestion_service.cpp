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

            void SetReviewStatus(
                jobpilot::core::ConnectionPool::Transaction &transaction,
                std::uint64_t jobId,
                std::int32_t status) override
            {
                transactions.push_back(&transaction);
                ++reviewStatusCalls;
                capturedJobIds.push_back(jobId);
                capturedReviewStatus = status;

                if (delegate_ != nullptr)
                {
                    delegate_->SetReviewStatus(
                        transaction,
                        jobId,
                        status);
                }
            }

            void InsertReviewItems(
                jobpilot::core::ConnectionPool::Transaction &transaction,
                std::uint64_t jobId,
                const std::vector<JobQualityFinding> &findings) override
            {
                transactions.push_back(&transaction);
                ++reviewItemCalls;
                capturedFindings = findings;

                if (delegate_ != nullptr)
                {
                    delegate_->InsertReviewItems(
                        transaction,
                        jobId,
                        findings);
                }
            }

            std::vector<
                jobpilot::core::ConnectionPool::Transaction *>
                transactions;
            std::size_t upsertCalls = 0;
            std::size_t replaceCalls = 0;
            std::size_t reviewStatusCalls = 0;
            std::size_t reviewItemCalls = 0;
            std::vector<std::uint64_t> capturedJobIds;
            std::int32_t capturedReviewStatus = -1;
            std::vector<JobQualityFinding> capturedFindings;
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

        // 0015A: source_url must be an absolute http(s) job-detail URL
        // (ExecPlan 0015A section 5.5.1); the fixtures below use this shape.
        std::string UniqueUrl(
            const std::string &prefix)
        {
            return "https://example.invalid/job/" +
                   jobpilot::test::UniqueJobToken(prefix);
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
                    UniqueUrl(
                        "too-many")));
        }

        EXPECT_EQ(
            service.Import(tooMany).code,
            ErrorCode::kInvalidParam);

        const std::string duplicateUrl =
            UniqueUrl(
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
                UniqueUrl(
                    "invalid-salary"));
        invalidSalary.job.salaryMin = 40;
        invalidSalary.job.salaryMax = 20;
        EXPECT_EQ(
            service.Import({invalidSalary}).code,
            ErrorCode::kInvalidParam);

        JobIngestionItem invalidLevel =
            MakeItem(
                UniqueUrl(
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
            UniqueUrl(
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
            UniqueUrl(
                "rollback-first");
        const std::string secondUrl =
            UniqueUrl(
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

    TEST_F(JobIngestionServiceTest, ValidatesThe0014ExtraFields)
    {
        RecordingJobIngestionDao dao;
        JobIngestionService service(dao, *pool);

        JobIngestionItem valid =
            MakeItem(
                UniqueUrl(
                    "extra-valid"));
        valid.job.educationRequired = 3;
        valid.job.experienceYearsMin = 0;
        valid.job.experienceYearsMax = 3;
        valid.job.jobNature = 1;
        valid.job.deadline = "2026-10-31 04:00:00";
        valid.job.deadlineDateOnly = true;
        EXPECT_EQ(
            service.Import({valid}).code,
            ErrorCode::kSuccess);

        JobIngestionItem explicitTime =
            MakeItem(
                UniqueUrl(
                    "extra-explicit"));
        explicitTime.job.deadline = "2026-10-31 10:30:00";
        explicitTime.job.deadlineDateOnly = false;
        EXPECT_EQ(
            service.Import({explicitTime}).code,
            ErrorCode::kSuccess);

        auto rejects = [&service](JobIngestionItem item)
        {
            EXPECT_EQ(
                service.Import({item}).code,
                ErrorCode::kInvalidParam);
        };

        JobIngestionItem educationTooHigh =
            MakeItem(
                UniqueUrl(
                    "extra-education"));
        educationTooHigh.job.educationRequired = 6;
        rejects(educationTooHigh);

        JobIngestionItem natureTooHigh =
            MakeItem(
                UniqueUrl(
                    "extra-nature"));
        natureTooHigh.job.jobNature = 4;
        rejects(natureTooHigh);

        JobIngestionItem yearsOverflow =
            MakeItem(
                UniqueUrl(
                    "extra-years"));
        yearsOverflow.job.experienceYearsMax = 32768;
        rejects(yearsOverflow);

        JobIngestionItem invertedYears =
            MakeItem(
                UniqueUrl(
                    "extra-years-inverted"));
        invertedYears.job.experienceYearsMin = 5;
        invertedYears.job.experienceYearsMax = 3;
        rejects(invertedYears);

        JobIngestionItem malformedDeadline =
            MakeItem(
                UniqueUrl(
                    "extra-deadline"));
        malformedDeadline.job.deadline = "2026-10-31T10:30:00";
        rejects(malformedDeadline);

        JobIngestionItem dateOnlyWithInstant =
            MakeItem(
                UniqueUrl(
                    "extra-kind"));
        dateOnlyWithInstant.job.deadline = "2026-10-31 10:30:00";
        dateOnlyWithInstant.job.deadlineDateOnly = true;
        rejects(dateOnlyWithInstant);

        JobIngestionItem flagWithoutDeadline =
            MakeItem(
                UniqueUrl(
                    "extra-flag"));
        flagWithoutDeadline.job.deadlineDateOnly = true;
        rejects(flagWithoutDeadline);

        // Only the two valid batches reached the DAO.
        EXPECT_EQ(dao.upsertCalls, 2U);
    }

    TEST_F(JobIngestionServiceTest, RecordsFindingsAndReviewStatus)
    {
        JobIngestionDao ingestionDao;
        RecordingJobIngestionDao dao(&ingestionDao);
        JobIngestionService service(
            dao,
            *pool,
            PlatformNameRegistry::FromNames({"牛客"}));

        const std::string cleanUrl = UniqueUrl("quality-clean");
        const std::string platformUrl = UniqueUrl("quality-platform");

        JobIngestionItem clean = MakeItem(cleanUrl);
        JobIngestionItem platform = MakeItem(platformUrl);
        platform.job.company = "牛客";
        JobIngestionItem homepage;
        homepage.job = MakeJob("https://example.invalid/");
        homepage.job.company = "Homepage Co";
        homepage.job.title = "Homepage Job";
        sourceUrls.push_back(homepage.job.sourceUrl);

        const ServiceResult<JobIngestionResult> result =
            service.Import({clean, platform, homepage});

        ASSERT_EQ(result.code, ErrorCode::kSuccess);
        EXPECT_EQ(result.value.created, 3U);
        EXPECT_EQ(result.value.quality.rejected, 0U);
        EXPECT_EQ(result.value.quality.needsReview, 2U);
        EXPECT_EQ(result.value.quality.clean, 1U);
        ASSERT_EQ(result.value.quality.items.size(), 3U);

        EXPECT_EQ(
            result.value.quality.items[0].reviewStatus,
            0);
        EXPECT_TRUE(
            result.value.quality.items[0].findings.empty());
        EXPECT_FALSE(
            result.value.quality.items[0].rejected);

        EXPECT_EQ(
            result.value.quality.items[1].reviewStatus,
            1);
        ASSERT_FALSE(
            result.value.quality.items[1].findings.empty());
        EXPECT_EQ(
            result.value.quality.items[1].findings[0].code,
            "employer_is_platform");
        EXPECT_EQ(
            result.value.quality.items[1].findings[0].outcome,
            JobQualityOutcome::kNeedsReview);

        EXPECT_EQ(
            result.value.quality.items[2].reviewStatus,
            1);
        ASSERT_FALSE(
            result.value.quality.items[2].findings.empty());
        EXPECT_EQ(
            result.value.quality.items[2].findings[0].code,
            "source_url_not_detail");
        EXPECT_EQ(
            result.value.quality.items[2].sourceUrl,
            "https://example.invalid/");

        // Persisted in the same transaction: the verdict on the row and the
        // open findings in job_review_items.
        const jobpilot::core::Result platformRow =
            pool->Query(
                "SELECT review_status FROM jobs WHERE source_url = ?",
                {platformUrl});

        ASSERT_EQ(platformRow.rowCount(), 1U);
        EXPECT_EQ(platformRow.value(0, 0).value(), "1");

        const jobpilot::core::Result platformItems =
            pool->Query(
                "SELECT reason_code, outcome, resolution "
                "FROM job_review_items "
                "WHERE job_id = (SELECT id FROM jobs "
                "WHERE source_url = ?)",
                {platformUrl});

        ASSERT_EQ(platformItems.rowCount(), 1U);
        EXPECT_EQ(
            platformItems.value(0, 0).value(),
            "employer_is_platform");
        EXPECT_EQ(
            platformItems.value(0, 1).value(),
            "needs_review");
        EXPECT_EQ(platformItems.value(0, 2).value(), "open");

        const jobpilot::core::Result cleanRow =
            pool->Query(
                "SELECT review_status FROM jobs WHERE source_url = ?",
                {cleanUrl});

        ASSERT_EQ(cleanRow.rowCount(), 1U);
        EXPECT_EQ(cleanRow.value(0, 0).value(), "0");

        EXPECT_EQ(dao.reviewStatusCalls, 3U);
        EXPECT_EQ(dao.reviewItemCalls, 3U);
    }

    TEST_F(JobIngestionServiceTest, RejectsUnusableInputWithADiagnostic)
    {
        RecordingJobIngestionDao dao;
        JobIngestionService service(dao, *pool);

        JobIngestionItem missingCompany =
            MakeItem(UniqueUrl("reject-company"));
        missingCompany.job.company = "   ";

        const ServiceResult<JobIngestionResult> companyRejected =
            service.Import({missingCompany});

        ASSERT_EQ(companyRejected.code, ErrorCode::kInvalidParam);
        EXPECT_EQ(
            companyRejected.value.quality.rejected,
            1U);
        EXPECT_EQ(
            companyRejected.value.quality.diagnosticCode,
            "company_missing");
        EXPECT_EQ(
            companyRejected.value.quality.diagnosticField,
            "company");
        EXPECT_FALSE(
            companyRejected.value.quality.diagnosticReason.empty());
        ASSERT_EQ(
            companyRejected.value.quality.items.size(),
            1U);
        EXPECT_TRUE(
            companyRejected.value.quality.items[0].rejected);

        JobIngestionItem unusableUrl =
            MakeItem("not-a-url");
        const ServiceResult<JobIngestionResult> urlRejected =
            service.Import({unusableUrl});

        ASSERT_EQ(urlRejected.code, ErrorCode::kInvalidParam);
        EXPECT_EQ(
            urlRejected.value.quality.diagnosticCode,
            "source_url_unusable");
        EXPECT_EQ(
            urlRejected.value.quality.diagnosticField,
            "source_url");

        JobIngestionItem hugeDescription =
            MakeItem(UniqueUrl("reject-description"));
        hugeDescription.job.description =
            std::string(kMaxDescriptionBytes + 1, 'a');

        const ServiceResult<JobIngestionResult> descriptionRejected =
            service.Import({hugeDescription});

        ASSERT_EQ(
            descriptionRejected.code,
            ErrorCode::kInvalidParam);
        EXPECT_EQ(
            descriptionRejected.value.quality.diagnosticCode,
            "description_too_large");
        EXPECT_EQ(
            descriptionRejected.value.quality.diagnosticField,
            "description");

        JobIngestionItem hugeCompany =
            MakeItem(UniqueUrl("reject-company-large"));
        hugeCompany.job.company = std::string(129, 'a');

        const ServiceResult<JobIngestionResult> companyCapRejected =
            service.Import({hugeCompany});

        ASSERT_EQ(companyCapRejected.code, ErrorCode::kInvalidParam);
        EXPECT_EQ(
            companyCapRejected.value.quality.diagnosticCode,
            "company_too_large");
        EXPECT_EQ(
            companyCapRejected.value.quality.diagnosticField,
            "company");

        EXPECT_EQ(dao.upsertCalls, 0U);
        EXPECT_EQ(dao.replaceCalls, 0U);

        const jobpilot::core::Result written =
            pool->Query(
                "SELECT COUNT(*) FROM jobs "
                "WHERE source_url LIKE "
                "'https://example.invalid/job/reject-%'",
                {});

        ASSERT_EQ(written.rowCount(), 1U);
        EXPECT_EQ(written.value(0, 0).value(), "0");
    }

    TEST_F(JobIngestionServiceTest, RegistryIsFailOpenAndAdvisoryStaysVisible)
    {
        JobIngestionDao ingestionDao;
        RecordingJobIngestionDao dao(&ingestionDao);
        JobIngestionService service(
            dao,
            *pool,
            PlatformNameRegistry::FromNames({"牛客"}));

        JobIngestionItem unknown =
            MakeItem(UniqueUrl("registry-unknown"));
        unknown.job.company = "Acme Corp";

        const ServiceResult<JobIngestionResult> unknownResult =
            service.Import({unknown});

        ASSERT_EQ(unknownResult.code, ErrorCode::kSuccess);
        EXPECT_EQ(
            unknownResult.value.quality.needsReview,
            0U);
        EXPECT_EQ(unknownResult.value.quality.rejected, 0U);
        EXPECT_TRUE(
            unknownResult.value.quality.items[0].findings.empty());

        // An unrecognised-but-http(s) shape is advisory: the row stays visible
        // with review_status = 0 (ExecPlan 0015A section 6.1 codes 9-11).
        JobIngestionItem advisory;
        advisory.job = MakeJob(
            "https://example.invalid/board/alpha");
        sourceUrls.push_back(advisory.job.sourceUrl);

        const ServiceResult<JobIngestionResult> advisoryResult =
            service.Import({advisory});

        ASSERT_EQ(advisoryResult.code, ErrorCode::kSuccess);
        EXPECT_EQ(advisoryResult.value.quality.clean, 1U);
        EXPECT_EQ(advisoryResult.value.quality.needsReview, 0U);
        ASSERT_EQ(
            advisoryResult.value.quality.items.size(),
            1U);
        ASSERT_FALSE(
            advisoryResult.value.quality.items[0].findings.empty());
        EXPECT_EQ(
            advisoryResult.value.quality.items[0].findings[0].code,
            "source_url_shape_unrecognised");
        EXPECT_EQ(
            advisoryResult.value.quality.items[0].findings[0].outcome,
            JobQualityOutcome::kAdvisory);

        const jobpilot::core::Result status =
            pool->Query(
                "SELECT review_status FROM jobs WHERE source_url = ?",
                {"https://example.invalid/board/alpha"});

        ASSERT_EQ(status.rowCount(), 1U);
        EXPECT_EQ(status.value(0, 0).value(), "0");

        // The DB stores the documented vocabulary verbatim: the widened
        // VARCHAR(24) column holds accepted_advisory (17 bytes).
        const jobpilot::core::Result outcome =
            pool->Query(
                "SELECT outcome FROM job_review_items "
                "WHERE job_id = (SELECT id FROM jobs "
                "WHERE source_url = ?)",
                {"https://example.invalid/board/alpha"});

        ASSERT_EQ(outcome.rowCount(), 1U);
        EXPECT_EQ(
            outcome.value(0, 0).value(),
            "accepted_advisory");
    }

} // namespace jobpilot::service
