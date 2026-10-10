#include "jobpilot/service/job/job_dao.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "jobpilot/database/mysql_pool.h"
#include "jobpilot/service/job/job_ingestion_dao.h"
#include "test_job_support.h"
#include "test_user_support.h"

namespace jobpilot::service
{
    class JobDaoTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            pool = jobpilot::test::MakeConnectionPool();
            dao = std::make_unique<JobDao>(*pool);
        }

        void TearDown() override
        {
            for (std::uint64_t jobId : jobIds)
            {
                jobpilot::test::CleanupJob(
                    pool.get(),
                    jobId);
            }
        }

        std::uint64_t AddJob(
            const std::string &company,
            const std::string &title,
            const std::optional<std::string> &location,
            const std::optional<std::string> &description,
            std::int32_t status,
            const std::string &updateTime)
        {
            const std::uint64_t id =
                jobpilot::test::InsertJob(
                    pool.get(),
                    company,
                    title,
                    location,
                    description,
                    jobpilot::test::UniqueJobToken(
                        "job-dao"),
                    status,
                    updateTime);
            jobIds.push_back(id);
            return id;
        }

        // 0014: write through the real manual upsert so the read-back path is
        // exercised end to end (parser/DTO construction happens elsewhere).
        std::uint64_t UpsertExtraJob(
            const JobImportData &job)
        {
            auto transaction = pool->BeginTransaction();
            JobIngestionDao ingestionDao;
            const JobUpsertResult result =
                ingestionDao.UpsertJob(transaction, job);
            transaction.Commit();
            jobIds.push_back(result.jobId);
            return result.jobId;
        }

        static JobImportData MakeExtraJob(
            const std::string &title,
            const std::string &sourceUrl)
        {
            JobImportData job;
            job.company = "Extra Company";
            job.title = title;
            job.source = "manual";
            job.sourceUrl = sourceUrl;
            return job;
        }

        std::unique_ptr<
            jobpilot::core::ConnectionPool>
            pool;
        std::unique_ptr<JobDao> dao;
        std::vector<std::uint64_t> jobIds;
    };
    TEST_F(JobDaoTest, SearchesFiltersAndSortsActiveJobs)
    {
        const std::string token =
            jobpilot::test::UniqueJobToken(
                "search-token");

        const std::uint64_t older =
            AddJob(
                "Acme",
                token + " C++ backend",
                "Shenzhen",
                "Linux service",
                1,
                "2026-10-01 10:00:00");

        const std::uint64_t newer =
            AddJob(
                "Beta",
                token + " C++ platform",
                "Shanghai",
                "Distributed systems",
                1,
                "2026-10-02 10:00:00");

        AddJob(
            "Hidden",
            token + " C++ inactive",
            "Shenzhen",
            std::nullopt,
            0,
            "2026-10-03 10:00:00");

        const std::vector<JobSummary> all =
            dao->SearchJobs(
                JobSearchFilter{
                    token,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt},
                20,
                0);

        ASSERT_EQ(all.size(), 2U);
        EXPECT_EQ(all[0].id, newer);
        EXPECT_EQ(all[1].id, older);
        EXPECT_EQ(
            dao->CountJobs(
                JobSearchFilter{
                    token,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt}),
            2U);

        const std::vector<JobSummary> byLocation =
            dao->SearchJobs(
                JobSearchFilter{
                    token,
                    std::string("Shenzhen"),
                    std::nullopt,
                    std::nullopt,
                    std::nullopt},
                20,
                0);

        ASSERT_EQ(byLocation.size(), 1U);
        EXPECT_EQ(byLocation.front().id, older);
    }

    // 0015A Step 1: the list projection exposes source_url, the embedded detail
    // summary exposes the identical value, and a NULL column projects as
    // nullopt rather than an empty string.
    TEST_F(JobDaoTest, ProjectsSourceUrlInListAndMatchesTheDetail)
    {
        const std::string token =
            jobpilot::test::UniqueJobToken("url-token");
        const std::string url =
            "https://example.invalid/jobs/" + token;

        const std::uint64_t id =
            UpsertExtraJob(
                MakeExtraJob(
                    token + " C++ url job",
                    url));

        const JobSearchFilter filter{
            token,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt};

        const std::vector<JobSummary> rows =
            dao->SearchJobs(filter, 20, 0);

        ASSERT_EQ(rows.size(), 1U);
        ASSERT_TRUE(rows.front().sourceUrl.has_value());
        EXPECT_EQ(rows.front().sourceUrl.value(), url);

        const std::optional<JobDetail> detail =
            dao->FindActiveJobById(id);

        ASSERT_TRUE(detail.has_value());
        ASSERT_TRUE(detail->sourceUrl.has_value());
        EXPECT_EQ(detail->sourceUrl.value(), url);
        ASSERT_TRUE(detail->summary.sourceUrl.has_value());
        EXPECT_EQ(
            detail->summary.sourceUrl.value(),
            detail->sourceUrl.value());

        pool->Execute(
            "UPDATE jobs SET source_url = NULL WHERE id = ?",
            {id});

        const std::vector<JobSummary> afterNull =
            dao->SearchJobs(filter, 20, 0);

        ASSERT_EQ(afterNull.size(), 1U);
        EXPECT_FALSE(afterNull.front().sourceUrl.has_value());
    }

    // 0015A item 3: education / job_nature / deadline enter the filter and sort
    // path, and CountJobs applies the identical predicate set (parity).
    TEST_F(JobDaoTest, FiltersAndSortsByThe0015AFocusFields)
    {
        const std::string token =
            jobpilot::test::UniqueJobToken("focus-token");

        JobImportData campusSoon =
            MakeExtraJob(
                token + " campus soon",
                token + "-campus-soon");
        campusSoon.educationRequired = 3;
        campusSoon.jobNature = 1;
        campusSoon.deadline = "2026-11-01 04:00:00";
        campusSoon.deadlineDateOnly = true;

        const std::uint64_t soonId =
            UpsertExtraJob(campusSoon);

        JobImportData campusLater =
            MakeExtraJob(
                token + " campus later",
                token + "-campus-later");
        campusLater.educationRequired = 5;
        campusLater.jobNature = 1;
        campusLater.deadline = "2026-12-01 04:00:00";
        campusLater.deadlineDateOnly = true;

        const std::uint64_t laterId =
            UpsertExtraJob(campusLater);

        // Unknown education and an unknown deadline must stay visible.
        JobImportData socialUnknown =
            MakeExtraJob(
                token + " social unknown",
                token + "-social-unknown");
        socialUnknown.jobNature = 2;

        const std::uint64_t socialId =
            UpsertExtraJob(socialUnknown);

        const auto searchWith =
            [this, &token](JobSearchFilter filter) {
                filter.keyword = token;
                return dao->SearchJobs(filter, 20, 0);
            };

        const auto countWith =
            [this, &token](JobSearchFilter filter) {
                filter.keyword = token;
                return dao->CountJobs(filter);
            };

        // education_max=3 keeps the NULL-education row and drops 5-master row.
        JobSearchFilter byEducation;
        byEducation.educationMax = 3;

        const std::vector<JobSummary> educationRows =
            searchWith(byEducation);

        ASSERT_EQ(educationRows.size(), 2U);
        EXPECT_EQ(countWith(byEducation), 2U);

        // job_nature=2 is an exact match.
        JobSearchFilter byNature;
        byNature.jobNature = 2;

        const std::vector<JobSummary> natureRows =
            searchWith(byNature);

        ASSERT_EQ(natureRows.size(), 1U);
        EXPECT_EQ(natureRows.front().id, socialId);
        EXPECT_EQ(countWith(byNature), 1U);

        // A deadline range excludes rows without a deadline.
        JobSearchFilter before;
        before.deadlineBefore = "2026-11-15 00:00:00";

        const std::vector<JobSummary> beforeRows =
            searchWith(before);

        ASSERT_EQ(beforeRows.size(), 1U);
        EXPECT_EQ(beforeRows.front().id, soonId);
        EXPECT_EQ(countWith(before), 1U);

        JobSearchFilter after;
        after.deadlineAfter = "2026-11-15 00:00:00";

        const std::vector<JobSummary> afterRows =
            searchWith(after);

        ASSERT_EQ(afterRows.size(), 1U);
        EXPECT_EQ(afterRows.front().id, laterId);

        // open_only keeps the known-future deadlines and the NULL deadline.
        JobSearchFilter open;
        open.openOnly = true;

        EXPECT_EQ(searchWith(open).size(), 3U);
        EXPECT_EQ(countWith(open), 3U);

        // deadline_asc is NULL-first (index-backed direction, docs/03 9.2).
        JobSearchFilter sorted;
        sorted.sort = JobSort::kDeadlineAsc;

        const std::vector<JobSummary> sortedRows =
            searchWith(sorted);

        ASSERT_EQ(sortedRows.size(), 3U);
        EXPECT_EQ(sortedRows[0].id, socialId);
        EXPECT_EQ(sortedRows[1].id, soonId);
        EXPECT_EQ(sortedRows[2].id, laterId);

        // education_asc is NULL-first as well.
        JobSearchFilter byEducationAsc;
        byEducationAsc.sort = JobSort::kEducationAsc;

        const std::vector<JobSummary> educationAscRows =
            searchWith(byEducationAsc);

        ASSERT_EQ(educationAscRows.size(), 3U);
        EXPECT_EQ(educationAscRows[0].id, socialId);
        EXPECT_EQ(educationAscRows[1].id, soonId);
        EXPECT_EQ(educationAscRows[2].id, laterId);
    }

    TEST_F(JobDaoTest, TreatsLikeWildcardsAsLiteralCharacters)
    {
        const std::string prefix =
            jobpilot::test::UniqueJobToken(
                "literal");

        AddJob(
            "Percent",
            prefix + "%",
            "Shenzhen",
            std::nullopt,
            1,
            "2026-10-01 10:00:00");

        AddJob(
            "Underscore",
            prefix + "_",
            "Shenzhen",
            std::nullopt,
            1,
            "2026-10-01 10:00:00");

        AddJob(
            "Exclamation",
            prefix + "!",
            "Shenzhen",
            std::nullopt,
            1,
            "2026-10-01 10:00:00");

        EXPECT_EQ(
            dao->CountJobs(
                JobSearchFilter{
                    prefix + "%",
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt}),
            1U);
        EXPECT_EQ(
            dao->CountJobs(
                JobSearchFilter{
                    prefix + "_",
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt}),
            1U);
        EXPECT_EQ(
            dao->CountJobs(
                JobSearchFilter{
                    prefix + "!",
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt}),
            1U);
        EXPECT_EQ(
            dao->CountJobs(
                JobSearchFilter{
                    "' OR 1=1 --",
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt}),
            0U);
    }

    TEST_F(JobDaoTest, LoadsActiveDetailAndRequirements)
    {
        const std::uint64_t active =
            AddJob(
                "Detail",
                "C++ backend",
                "Shenzhen",
                "Detailed description",
                1,
                "2026-10-01 10:00:00");

        jobpilot::test::InsertJobRequirement(
            pool.get(),
            active,
            "Redis",
            2);
        jobpilot::test::InsertJobRequirement(
            pool.get(),
            active,
            "C++",
            3);

        const std::optional<JobDetail> detail =
            dao->FindActiveJobById(active);

        ASSERT_TRUE(detail.has_value());
        EXPECT_EQ(
            detail->description,
            "Detailed description");

        const std::vector<JobRequirement> requirements =
            dao->ListRequirements(active);

        ASSERT_EQ(requirements.size(), 2U);
        EXPECT_EQ(requirements[0].skill, "C++");
        EXPECT_EQ(requirements[0].level, 3);
        EXPECT_EQ(requirements[1].skill, "Redis");

        const std::uint64_t inactive =
            AddJob(
                "Hidden",
                "Inactive",
                "Shenzhen",
                std::nullopt,
                0,
                "2026-10-01 10:00:00");

        EXPECT_FALSE(
            dao->FindActiveJobById(inactive)
                .has_value());
        EXPECT_FALSE(
            dao->FindActiveJobById(999999999ULL)
                .has_value());
    }

    TEST_F(JobDaoTest, FiltersSalaryAndSkillAndLoadsBulkRequirements)
    {
        const std::string token =
            jobpilot::test::UniqueJobToken(
                "salary-skill");
        const std::uint64_t matching =
            AddJob(
                "Match",
                token + " C++",
                "Shenzhen",
                "description",
                1,
                "2026-10-01 10:00:00");
        const std::uint64_t other =
            AddJob(
                "Other",
                token + " Go",
                "Shenzhen",
                "description",
                1,
                "2026-10-02 10:00:00");

        pool->Execute(
            "UPDATE jobs SET salary_min = ?, salary_max = ? "
            "WHERE id = ?",
            {std::uint64_t(20),
             std::uint64_t(35),
             matching});
        pool->Execute(
            "UPDATE jobs SET salary_min = ?, salary_max = ? "
            "WHERE id = ?",
            {std::uint64_t(50),
             std::uint64_t(70),
             other});

        jobpilot::test::InsertJobRequirement(
            pool.get(),
            matching,
            "C++",
            3);
        jobpilot::test::InsertJobRequirement(
            pool.get(),
            other,
            "Go",
            2);

        const JobSearchFilter filter{
            token,
            std::nullopt,
            std::uint32_t(25),
            std::uint32_t(40),
            std::string("C++")};

        const std::vector<JobSummary> jobs =
            dao->SearchJobs(filter, 20, 0);

        ASSERT_EQ(jobs.size(), 1U);
        EXPECT_EQ(jobs.front().id, matching);
        EXPECT_EQ(dao->CountJobs(filter), 1U);

        const std::vector<JobRequirementsRecord> records =
            dao->ListRequirementsForJobs(
                {matching, other});

        ASSERT_EQ(records.size(), 2U);
        EXPECT_EQ(records[0].jobId, matching);
        ASSERT_EQ(records[0].requirements.size(), 1U);
        EXPECT_EQ(records[0].requirements[0].skill, "C++");
        EXPECT_EQ(records[1].jobId, other);
        ASSERT_EQ(records[1].requirements.size(), 1U);
        EXPECT_EQ(records[1].requirements[0].skill, "Go");
    }
    TEST_F(JobDaoTest, AppliesFreshnessVisibilityAndFields)
    {
        const std::string token =
            jobpilot::test::UniqueJobToken(
                "freshness");

        const std::uint64_t legacy =
            AddJob(
                "Legacy",
                token + " legacy",
                "Shenzhen",
                std::nullopt,
                1,
                "2026-10-01 10:00:00");
        const std::uint64_t fresh =
            AddJob(
                "Fresh",
                token + " fresh",
                "Shenzhen",
                std::nullopt,
                1,
                "2026-10-02 10:00:00");
        const std::uint64_t stale =
            AddJob(
                "Stale",
                token + " stale",
                "Shenzhen",
                std::nullopt,
                1,
                "2026-10-03 10:00:00");
        const std::uint64_t closed =
            AddJob(
                "Closed",
                token + " closed",
                "Shenzhen",
                std::nullopt,
                1,
                "2026-10-04 10:00:00");

        pool->Execute(
            "UPDATE jobs SET "
            "last_seen_at = TIMESTAMPADD(HOUR, -1, NOW()), "
            "last_verified_at = TIMESTAMPADD(HOUR, -1, NOW()), "
            "published_at = TIMESTAMPADD(DAY, -2, NOW()), "
            "source_status = 'ok' WHERE id = ?",
            {fresh});
        pool->Execute(
            "UPDATE jobs SET "
            "last_seen_at = TIMESTAMPADD(DAY, -8, NOW()), "
            "last_verified_at = TIMESTAMPADD(DAY, -8, NOW()), "
            "published_at = TIMESTAMPADD(DAY, -30, NOW()), "
            "source_status = 'ok' WHERE id = ?",
            {stale});
        pool->Execute(
            "UPDATE jobs SET closed_at = NOW(), "
            "source_status = 'closed' WHERE id = ?",
            {closed});

        JobSearchFilter filter;
        filter.keyword = token;

        const std::vector<JobSummary> visible =
            dao->SearchJobs(filter, 20, 0);

        ASSERT_EQ(visible.size(), 2U);
        EXPECT_EQ(dao->CountJobs(filter), 2U);

        bool sawFresh = false;
        bool sawLegacy = false;

        for (const JobSummary &job : visible)
        {
            EXPECT_NE(job.id, stale);
            EXPECT_NE(job.id, closed);

            if (job.id == fresh)
            {
                sawFresh = true;
                ASSERT_TRUE(job.freshness.has_value());
                EXPECT_EQ(
                    job.freshness.value(),
                    FreshnessState::kFresh);
                ASSERT_TRUE(job.sourceStatus.has_value());
                EXPECT_EQ(job.sourceStatus.value(), "ok");
                EXPECT_TRUE(job.publishedAt.has_value());
                EXPECT_TRUE(job.lastSeenAt.has_value());
                EXPECT_TRUE(job.lastVerifiedAt.has_value());
            }

            if (job.id == legacy)
            {
                sawLegacy = true;
                EXPECT_FALSE(job.freshness.has_value());
                EXPECT_FALSE(job.lastSeenAt.has_value());
                EXPECT_FALSE(job.publishedAt.has_value());
                EXPECT_FALSE(job.createTime.empty());
            }
        }

        EXPECT_TRUE(sawFresh);
        EXPECT_TRUE(sawLegacy);

        filter.includeStale = true;

        const std::vector<JobSummary> withStale =
            dao->SearchJobs(filter, 20, 0);

        ASSERT_EQ(withStale.size(), 3U);
        EXPECT_EQ(dao->CountJobs(filter), 3U);

        bool sawStale = false;

        for (const JobSummary &job : withStale)
        {
            EXPECT_NE(job.id, closed);

            if (job.id == stale)
            {
                sawStale = true;
                ASSERT_TRUE(job.freshness.has_value());
                EXPECT_EQ(
                    job.freshness.value(),
                    FreshnessState::kStale);
            }
        }

        EXPECT_TRUE(sawStale);

        filter.postedWithinDays = std::uint32_t(7);

        const std::vector<JobSummary> recent =
            dao->SearchJobs(filter, 20, 0);

        ASSERT_EQ(recent.size(), 1U);
        EXPECT_EQ(recent.front().id, fresh);
        EXPECT_EQ(dao->CountJobs(filter), 1U);

        filter.postedWithinDays = std::uint32_t(31);

        EXPECT_EQ(
            dao->SearchJobs(filter, 20, 0).size(),
            2U);
    }

    TEST_F(JobDaoTest, ReadsBackThe0014FieldsForBothDeadlineKinds)
    {
        const std::string token =
            jobpilot::test::UniqueJobToken("extra-fields");

        JobImportData dateOnly =
            MakeExtraJob(
                token + " date-only",
                token + "-date-only");
        dateOnly.educationRequired = 3;
        dateOnly.experienceYearsMin = 0;
        dateOnly.experienceYearsMax = 3;
        dateOnly.jobNature = 1;
        dateOnly.deadline = "2026-10-31 04:00:00";
        dateOnly.deadlineDateOnly = true;

        const std::uint64_t dateOnlyId =
            UpsertExtraJob(dateOnly);

        JobImportData explicitTime =
            MakeExtraJob(
                token + " explicit",
                token + "-explicit");
        explicitTime.deadline = "2026-10-31 10:30:00";
        explicitTime.deadlineDateOnly = false;

        const std::uint64_t explicitId =
            UpsertExtraJob(explicitTime);

        // Kind undetermined (administrative/legacy write): the DAO stores NULL
        // and the read-back must not fabricate a kind.
        JobImportData unknownKind =
            MakeExtraJob(
                token + " unknown",
                token + "-unknown");
        unknownKind.deadline = "2026-10-31 10:30:00";

        const std::uint64_t unknownId =
            UpsertExtraJob(unknownKind);

        JobImportData noDeadline =
            MakeExtraJob(
                token + " empty",
                token + "-empty");

        const std::uint64_t emptyId =
            UpsertExtraJob(noDeadline);

        const std::optional<JobDetail> dateOnlyDetail =
            dao->FindActiveJobById(dateOnlyId);

        ASSERT_TRUE(dateOnlyDetail.has_value());
        EXPECT_EQ(
            dateOnlyDetail->summary.educationRequired,
            std::optional<std::uint32_t>(3));
        EXPECT_EQ(
            dateOnlyDetail->summary.experienceYearsMin,
            std::optional<std::uint32_t>(0));
        EXPECT_EQ(
            dateOnlyDetail->summary.experienceYearsMax,
            std::optional<std::uint32_t>(3));
        EXPECT_EQ(
            dateOnlyDetail->summary.jobNature,
            std::optional<std::uint32_t>(1));
        // The DAO returns the stored UTC instant; Beijing rendering is the API
        // boundary's job (docs/03 section 9.1).
        EXPECT_EQ(
            dateOnlyDetail->summary.deadline,
            std::optional<std::string>("2026-10-31 04:00:00"));
        EXPECT_EQ(
            dateOnlyDetail->summary.deadlineDateOnly,
            std::optional<bool>(true));

        const std::optional<JobDetail> explicitDetail =
            dao->FindActiveJobById(explicitId);

        ASSERT_TRUE(explicitDetail.has_value());
        EXPECT_EQ(
            explicitDetail->summary.deadline,
            std::optional<std::string>("2026-10-31 10:30:00"));
        EXPECT_EQ(
            explicitDetail->summary.deadlineDateOnly,
            std::optional<bool>(false));

        const std::optional<JobDetail> unknownDetail =
            dao->FindActiveJobById(unknownId);

        ASSERT_TRUE(unknownDetail.has_value());
        EXPECT_EQ(
            unknownDetail->summary.deadline,
            std::optional<std::string>("2026-10-31 10:30:00"));
        EXPECT_FALSE(
            unknownDetail->summary.deadlineDateOnly.has_value());

        const std::optional<JobDetail> emptyDetail =
            dao->FindActiveJobById(emptyId);

        ASSERT_TRUE(emptyDetail.has_value());
        EXPECT_FALSE(
            emptyDetail->summary.deadline.has_value());
        EXPECT_FALSE(
            emptyDetail->summary.deadlineDateOnly.has_value());
        EXPECT_FALSE(
            emptyDetail->summary.educationRequired.has_value());

        JobSearchFilter filter;
        filter.keyword = token;

        const std::vector<JobSummary> jobs =
            dao->SearchJobs(filter, 20, 0);

        ASSERT_EQ(jobs.size(), 4U);

        bool sawDateOnly = false;

        for (const JobSummary &job : jobs)
        {
            if (job.id != dateOnlyId)
            {
                continue;
            }

            sawDateOnly = true;
            EXPECT_EQ(
                job.educationRequired,
                std::optional<std::uint32_t>(3));
            EXPECT_EQ(
                job.jobNature,
                std::optional<std::uint32_t>(1));
            EXPECT_EQ(
                job.deadline,
                std::optional<std::string>("2026-10-31 04:00:00"));
            EXPECT_EQ(
                job.deadlineDateOnly,
                std::optional<bool>(true));
        }

        EXPECT_TRUE(sawDateOnly);
    }

    // 0015A item 4: the public predicate is the stage's single behavioural
    // change and only clean rows are returned unless the switch is enabled.
    TEST_F(JobDaoTest, PublicVisibilityExcludesNeedsReviewJobs)
    {
        const std::string token =
            jobpilot::test::UniqueJobToken("review-visibility");

        const std::uint64_t cleanId = AddJob(
            "Review Co",
            token + " clean",
            std::nullopt,
            std::nullopt,
            1,
            "2026-10-01 10:00:00");
        const std::uint64_t hiddenId = AddJob(
            "Review Co",
            token + " hidden",
            std::nullopt,
            std::nullopt,
            1,
            "2026-10-01 10:00:00");

        pool->Execute(
            "UPDATE jobs SET review_status = 1 WHERE id = ?",
            {hiddenId});

        JobSearchFilter filter;
        filter.keyword = token;

        const std::vector<JobSummary> visible =
            dao->SearchJobs(filter, 20, 0);

        ASSERT_EQ(visible.size(), 1U);
        EXPECT_EQ(visible[0].id, cleanId);
        EXPECT_EQ(dao->CountJobs(filter), 1U);

        const std::vector<JobSummary> rankedVisible =
            dao->SearchRankingCandidates(filter, 20);

        ASSERT_EQ(rankedVisible.size(), 1U);
        EXPECT_EQ(rankedVisible[0].id, cleanId);

        JobSearchFilter withHidden = filter;
        withHidden.includeNeedsReview = true;

        const std::vector<JobSummary> all =
            dao->SearchJobs(withHidden, 20, 0);

        ASSERT_EQ(all.size(), 2U);
        EXPECT_EQ(dao->CountJobs(withHidden), 2U);

        const std::vector<JobSummary> rankedAll =
            dao->SearchRankingCandidates(withHidden, 20);

        ASSERT_EQ(rankedAll.size(), 2U);
    }
} // namespace jobpilot::service
