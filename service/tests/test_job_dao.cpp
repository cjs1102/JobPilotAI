#include "jobpilot/service/job/job_dao.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "jobpilot/database/mysql_pool.h"
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
} // namespace jobpilot::service
