#include "jobpilot/service/ai/match_rule.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "jobpilot/service/job/job_ranking.h"

namespace jobpilot::service
{
    namespace
    {
        const std::chrono::system_clock::time_point kNow =
            std::chrono::system_clock::now();

        std::string FormatLocal(
            std::chrono::system_clock::time_point moment)
        {
            const std::time_t seconds =
                std::chrono::system_clock::to_time_t(moment);

            std::tm parts{};
            localtime_r(&seconds, &parts);

            char buffer[32] = {};
            std::strftime(
                buffer,
                sizeof(buffer),
                "%Y-%m-%d %H:%M:%S",
                &parts);

            return buffer;
        }

        JobSummary MakeJob(
            const std::string &title = "C++ Backend Engineer",
            std::optional<std::uint32_t> salaryMin =
                std::optional<std::uint32_t>(20),
            std::optional<std::uint32_t> salaryMax =
                std::optional<std::uint32_t>(35),
            std::optional<std::string> location =
                std::optional<std::string>("Shenzhen"),
            std::string createTime = std::string())
        {
            JobSummary job;
            job.id = 20001;
            job.company = "Example";
            job.title = title;
            job.salaryMin = salaryMin;
            job.salaryMax = salaryMax;
            job.salaryCurrency = "CNY";
            job.location = location;
            job.source = "greenhouse";
            job.createTime = createTime.empty()
                                 ? FormatLocal(
                                       kNow -
                                       std::chrono::hours(1))
                                 : createTime;
            job.updateTime = job.createTime;
            return job;
        }

        std::vector<JobRequirement> MakeRequirements()
        {
            return {
                JobRequirement{"C++", 2},
                JobRequirement{"Redis", 1},
                JobRequirement{"Kubernetes", 1}};
        }

        std::vector<UserSkill> MakeSkills()
        {
            return {
                UserSkill{1, 10, "C++", 3, ""},
                UserSkill{2, 10, "Redis", 3, ""}};
        }

        UserProfile MakeProfile(
            std::optional<std::uint32_t> salaryMin =
                std::optional<std::uint32_t>(20),
            std::optional<std::uint32_t> salaryMax =
                std::optional<std::uint32_t>(35))
        {
            UserProfile profile;
            profile.userId = 10;
            profile.targetPosition =
                std::optional<std::string>("C++ Backend Engineer");
            profile.location =
                std::optional<std::string>("Shenzhen");
            profile.salaryMin = salaryMin;
            profile.salaryMax = salaryMax;
            profile.summary = std::optional<std::string>("Backend");
            return profile;
        }

        bool HasEvidence(
            const std::vector<MatchEvidenceItem> &items,
            const std::string &substring,
            const char *evidence)
        {
            for (const MatchEvidenceItem &item : items)
            {
                if (item.evidence == evidence &&
                    item.text.find(substring) != std::string::npos)
                {
                    return true;
                }
            }

            return false;
        }
    } // namespace

    TEST(MatchRuleTest, IsDeterministicAndBounded)
    {
        const JobSummary job = MakeJob();
        const std::vector<JobRequirement> requirements =
            MakeRequirements();
        const std::vector<UserSkill> skills = MakeSkills();

        const MatchResult first = ScoreMatch(
            job,
            requirements,
            MakeProfile(),
            skills,
            "Experienced with C++ and Redis.",
            kNow);

        const MatchResult second = ScoreMatch(
            job,
            requirements,
            MakeProfile(),
            skills,
            "Experienced with C++ and Redis.",
            kNow);

        EXPECT_EQ(first.score, second.score);
        EXPECT_GE(first.score, 0.0);
        EXPECT_LE(first.score, 100.0);
        EXPECT_EQ(std::floor(first.score), first.score);
        EXPECT_EQ(first.scoreVersion, "match_rule_v1");
        EXPECT_EQ(first.source, "rule");
        EXPECT_EQ(first.breakdown.skill, second.breakdown.skill);
    }

    TEST(MatchRuleTest, IgnoresRecencyUnlikeRuleV1)
    {
        const std::vector<JobRequirement> requirements =
            MakeRequirements();
        const std::vector<UserSkill> skills = MakeSkills();
        const std::string resume = "Experienced with C++ and Redis.";

        const JobSummary fresh = MakeJob(
            "C++ Backend Engineer",
            std::optional<std::uint32_t>(20),
            std::optional<std::uint32_t>(35),
            std::optional<std::string>("Shenzhen"),
            FormatLocal(kNow - std::chrono::hours(1)));

        const JobSummary old = MakeJob(
            "C++ Backend Engineer",
            std::optional<std::uint32_t>(20),
            std::optional<std::uint32_t>(35),
            std::optional<std::string>("Shenzhen"),
            FormatLocal(kNow - std::chrono::hours(24 * 40)));

        const MatchResult freshMatch = ScoreMatch(
            fresh,
            requirements,
            MakeProfile(),
            skills,
            resume,
            kNow);
        const MatchResult oldMatch = ScoreMatch(
            old,
            requirements,
            MakeProfile(),
            skills,
            resume,
            kNow);

        EXPECT_EQ(freshMatch.score, oldMatch.score);

        const RankedJob freshRanked = ScoreJob(
            fresh,
            requirements,
            MakeProfile(),
            skills,
            JobSearchFilter{},
            kNow);
        const RankedJob oldRanked = ScoreJob(
            old,
            requirements,
            MakeProfile(),
            skills,
            JobSearchFilter{},
            kNow);

        EXPECT_GT(freshRanked.score, oldRanked.score);

        // Regression: over-qualified skills must not push rule_v1 above 100.
        EXPECT_LE(freshRanked.score, 100.0);
        EXPECT_LE(oldRanked.score, 100.0);
        EXPECT_GT(freshRanked.score, 99.0);

        // 0013 match_rule_v1 drops recency, so a perfect fit is exactly 100.
        EXPECT_DOUBLE_EQ(freshMatch.score, 100.0);

        const MatchResult unmatched = ScoreMatch(
            old,
            requirements,
            MakeProfile(),
            {},
            resume,
            kNow);

        EXPECT_LT(unmatched.score, freshMatch.score);
    }

    TEST(MatchRuleTest, ReportsMatchedAndMissingSkills)
    {
        const MatchResult result = ScoreMatch(
            MakeJob(),
            MakeRequirements(),
            MakeProfile(),
            MakeSkills(),
            "Experienced with C++ and Redis.",
            kNow);

        ASSERT_EQ(result.matchedSkills.size(), 2U);
        EXPECT_EQ(result.matchedSkills[0], "C++");
        EXPECT_EQ(result.matchedSkills[1], "Redis");

        ASSERT_EQ(result.missingSkills.size(), 1U);
        EXPECT_EQ(result.missingSkills[0], "Kubernetes");

        EXPECT_TRUE(HasEvidence(
            result.strengths,
            "Matched 2/3 required skills",
            kEvidenceSkills));
        EXPECT_TRUE(HasEvidence(
            result.gaps,
            "Missing required skill: Kubernetes",
            kEvidenceJobRequirements));
    }

    TEST(MatchRuleTest, ReportsProfileComponentEvidence)
    {
        const MatchResult aligned = ScoreMatch(
            MakeJob(),
            MakeRequirements(),
            MakeProfile(),
            MakeSkills(),
            "Experienced with C++ and Redis.",
            kNow);

        EXPECT_TRUE(HasEvidence(
            aligned.strengths,
            "Salary expectation overlaps",
            kEvidenceProfile));
        EXPECT_TRUE(HasEvidence(
            aligned.strengths,
            "Location matches",
            kEvidenceProfile));
        EXPECT_TRUE(HasEvidence(
            aligned.strengths,
            "Title relevance",
            kEvidenceProfile));

        const MatchResult mismatched = ScoreMatch(
            MakeJob(),
            MakeRequirements(),
            MakeProfile(
                std::optional<std::uint32_t>(100),
                std::optional<std::uint32_t>(120)),
            MakeSkills(),
            "Experienced with C++ and Redis.",
            kNow);

        EXPECT_TRUE(HasEvidence(
            mismatched.gaps,
            "does not overlap the posting range",
            kEvidenceProfile));
    }

    TEST(MatchRuleTest, HandlesMissingProfileAndRequirements)
    {
        const MatchResult empty = ScoreMatch(
            MakeJob(),
            {},
            std::nullopt,
            {},
            "",
            kNow);

        EXPECT_EQ(empty.score, 0.0);
        EXPECT_FALSE(empty.breakdown.skill.has_value());
        EXPECT_FALSE(empty.breakdown.title.has_value());
        EXPECT_FALSE(empty.breakdown.salary.has_value());
        EXPECT_FALSE(empty.breakdown.location.has_value());
        EXPECT_TRUE(empty.matchedSkills.empty());
        EXPECT_TRUE(empty.missingSkills.empty());

        EXPECT_TRUE(HasEvidence(
            empty.gaps,
            "no structured skill requirements",
            kEvidenceJobRequirements));
        EXPECT_TRUE(HasEvidence(
            empty.gaps,
            "Profile is empty",
            kEvidenceProfile));
        EXPECT_TRUE(empty.resumeEvidence.empty());

        const MatchResult sparse = ScoreMatch(
            MakeJob(
                "C++ Backend Engineer",
                std::nullopt,
                std::nullopt,
                std::nullopt),
            {},
            MakeProfile(),
            {},
            "",
            kNow);

        EXPECT_TRUE(HasEvidence(
            sparse.gaps,
            "salary fit cannot be evaluated",
            kEvidenceProfile));
        EXPECT_TRUE(HasEvidence(
            sparse.gaps,
            "location fit cannot be evaluated",
            kEvidenceProfile));
    }

    TEST(MatchRuleTest, ResumeEvidenceUsesWordBoundaries)
    {
        const std::vector<JobRequirement> requirements = {
            JobRequirement{"Redis", 1},
            JobRequirement{"C++", 1},
            JobRequirement{"Red", 1},
            JobRequirement{"Go", 1}};

        const MatchResult result = ScoreMatch(
            MakeJob(),
            requirements,
            MakeProfile(),
            MakeSkills(),
            "Experienced with Redis and C++ on Linux.",
            kNow);

        ASSERT_EQ(result.resumeEvidence.size(), 4U);
        EXPECT_TRUE(result.resumeEvidence[0].mentioned);
        EXPECT_TRUE(result.resumeEvidence[1].mentioned);
        EXPECT_FALSE(result.resumeEvidence[2].mentioned);
        EXPECT_FALSE(result.resumeEvidence[3].mentioned);

        EXPECT_TRUE(ResumeMentionsSkill("uses redis daily", "Redis"));
        EXPECT_TRUE(ResumeMentionsSkill("C++ / Linux", "c++"));
        EXPECT_FALSE(ResumeMentionsSkill("rediscluster operator", "Redis"));
        EXPECT_FALSE(ResumeMentionsSkill("", "Redis"));
        EXPECT_FALSE(ResumeMentionsSkill("Redis", "  "));
    }

} // namespace jobpilot::service
