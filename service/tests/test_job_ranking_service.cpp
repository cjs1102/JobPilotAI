#include "jobpilot/service/job/job_ranking_service.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace jobpilot::service
{
    namespace
    {
        class FakeJobRankingDao final
            : public IJobRankingDao
        {
        public:
            std::vector<JobSummary> SearchRankingCandidates(
                const JobSearchFilter &,
                std::size_t limit) override
            {
                capturedLimit = limit;

                if (candidates.size() > limit)
                {
                    return std::vector<JobSummary>(
                        candidates.begin(),
                        candidates.begin() +
                            static_cast<std::ptrdiff_t>(
                                limit));
                }

                return candidates;
            }

            std::size_t CountRankingCandidates(
                const JobSearchFilter &) override
            {
                return total;
            }

            std::vector<JobSummary> SearchListWindow(
                const JobSearchFilter &,
                std::size_t limit) override
            {
                capturedWindowLimit = limit;

                if (candidates.size() > limit)
                {
                    return std::vector<JobSummary>(
                        candidates.begin(),
                        candidates.begin() +
                            static_cast<std::ptrdiff_t>(
                                limit));
                }

                return candidates;
            }

            std::vector<JobRequirementsRecord>
            ListRequirementsForJobs(
                const std::vector<std::uint64_t> &) override
            {
                return requirements;
            }

            std::vector<JobSummary> candidates;
            std::vector<JobRequirementsRecord> requirements;
            std::size_t total = 0;
            std::size_t capturedLimit = 0;
            std::size_t capturedWindowLimit = 0;
        };

        class FakeProfileReader final
            : public IUserProfileReader
        {
        public:
            std::optional<UserProfile> FindProfile(
                std::uint64_t) override
            {
                return profile;
            }

            std::vector<UserSkill> ListSkills(
                std::uint64_t) override
            {
                return skills;
            }

            std::optional<UserProfile> profile;
            std::vector<UserSkill> skills;
        };

        class FakeAuthenticator final
            : public ISessionAuthenticator
        {
        public:
            ServiceResult<std::uint64_t> Authenticate(
                const std::string &) override
            {
                return ServiceResult<std::uint64_t>{
                    ErrorCode::kUnauthorized,
                    0};
            }
        };

        JobSummary MakeJob(
            std::uint64_t id,
            const std::string &title,
            const std::string &createTime)
        {
            JobSummary job;
            job.id = id;
            job.company = "Example";
            job.title = title;
            job.location = "Shenzhen";
            job.salaryMin = 20;
            job.salaryMax = 30;
            job.source = "import";
            job.createTime = createTime;
            job.updateTime = createTime;
            return job;
        }
    } // namespace

    TEST(JobRankingTest, SuppressesHardFilteredComponents)
    {
        UserProfile profile;
        profile.targetPosition = "C++ Backend";
        profile.location = "Shenzhen";
        profile.salaryMin = 20;
        profile.salaryMax = 30;

        const std::vector<UserSkill> skills = {
            UserSkill{1, 1, "C++", 3, ""},
            UserSkill{2, 1, "Linux", 2, ""}};
        const std::vector<JobRequirement> requirements = {
            JobRequirement{"C++", 3},
            JobRequirement{"Kubernetes", 2}};
        const JobSearchFilter filter{
            std::nullopt,
            std::string("Shenzhen"),
            std::uint32_t(20),
            std::uint32_t(30),
            std::string("C++")};

        std::tm time{};
        std::istringstream input(
            "2026-10-01 00:00:00");
        input >> std::get_time(
            &time,
            "%Y-%m-%d %H:%M:%S");
        const auto now =
            std::chrono::system_clock::from_time_t(
                std::mktime(&time));

        const RankedJob ranked =
            ScoreJob(
                MakeJob(
                    10,
                    "C++ Backend Engineer",
                    "2026-10-01 00:00:00"),
                requirements,
                profile,
                skills,
                filter,
                now);

        EXPECT_FALSE(ranked.breakdown.skill.has_value());
        EXPECT_FALSE(ranked.breakdown.salary.has_value());
        EXPECT_FALSE(ranked.breakdown.location.has_value());
        EXPECT_DOUBLE_EQ(ranked.breakdown.title.value(), 1.0);
        EXPECT_DOUBLE_EQ(ranked.breakdown.firstSeen.value(), 1.0);
        EXPECT_DOUBLE_EQ(ranked.score, 100.0);
        EXPECT_EQ(ranked.scoreVersion, "rule_v1");
        ASSERT_EQ(ranked.matchedSkills.size(), 1U);
        EXPECT_EQ(ranked.matchedSkills[0], "C++");
        ASSERT_EQ(ranked.missingSkills.size(), 1U);
        EXPECT_EQ(
            ranked.missingSkills[0],
            "Kubernetes");
    }

    TEST(JobRankingServiceTest, SortsPaginatesAndReportsTruncation)
    {
        FakeJobRankingDao dao;
        dao.total = 2500;
        dao.candidates = {
            MakeJob(
                1,
                "C++ Backend",
                "2026-10-01 00:00:00"),
            MakeJob(
                2,
                "Go Backend",
                "2026-10-02 00:00:00")};
        dao.requirements = {
            {1, {{"C++", 3}}},
            {2, {{"Go", 3}}}};

        FakeProfileReader reader;
        UserProfile profile;
        profile.targetPosition = "C++";
        reader.profile = profile;
        reader.skills = {
            UserSkill{1, 1, "C++", 3, ""}};

        FakeAuthenticator authenticator;
        JobRankingService service(
            dao,
            reader,
            authenticator);

        const ServiceResult<RankedJobPage> result =
            service.RankJobs(
                1,
                {},
                1,
                1);

        ASSERT_EQ(result.code, ErrorCode::kSuccess);
        EXPECT_EQ(result.value.total, 2500U);
        EXPECT_EQ(result.value.rankedTotal, 2U);
        EXPECT_TRUE(result.value.truncated);
        ASSERT_EQ(result.value.list.size(), 1U);
        EXPECT_EQ(result.value.list[0].job.id, 1U);
        EXPECT_GT(
            result.value.list[0].score,
            0.0);
    }

    TEST(JobRankingServiceTest, RequiresUsableProfileOrSkills)
    {
        FakeJobRankingDao dao;
        FakeProfileReader reader;
        FakeAuthenticator authenticator;
        JobRankingService service(
            dao,
            reader,
            authenticator);

        EXPECT_EQ(
            service.RankJobs(1, {}, 1, 20).code,
            ErrorCode::kInvalidParam);
    }

} // namespace jobpilot::service
