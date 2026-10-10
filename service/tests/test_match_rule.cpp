#include "jobpilot/service/ai/match_rule.h"

#include <algorithm>
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

    // 0015 B0/B1: the deterministic explanation inputs — deadline urgency,
    // deal-breakers as DATA, and the NULL rules. Nothing here re-weights the
    // rule_v1 score.
    TEST(MatchExplanationTest, ClassifiesDeadlineUrgencyWithDateOnlyHandling)
    {
        const auto now =
            std::chrono::system_clock::from_time_t(1800000000);

        auto format = [](std::int64_t offsetSeconds)
        {
            const std::time_t moment =
                static_cast<std::time_t>(1800000000 + offsetSeconds);
            std::tm parts{};
            gmtime_r(&moment, &parts);

            char buffer[32] = {};
            std::strftime(
                buffer,
                sizeof(buffer),
                "%Y-%m-%d %H:%M:%S",
                &parts);

            return std::string(buffer);
        };

        // A NULL deadline is unknown and is never expired.
        EXPECT_EQ(
            ClassifyDeadlineUrgency(std::nullopt, std::nullopt, now),
            DeadlineUrgency::kUnknown);
        // ToString returns const char *, so this must compare contents (the
        // literal is not guaranteed to be pooled), as elsewhere in the suite.
        EXPECT_STREQ(
            ToString(DeadlineUrgency::kUnknown),
            "unknown");

        // Past / <=7d / <=30d / beyond, exact-instant deadlines.
        EXPECT_EQ(
            ClassifyDeadlineUrgency(
                format(-60),
                false,
                now),
            DeadlineUrgency::kExpired);
        EXPECT_EQ(
            ClassifyDeadlineUrgency(
                format(6 * 24 * 3600),
                false,
                now),
            DeadlineUrgency::kUrgent);
        EXPECT_EQ(
            ClassifyDeadlineUrgency(
                format(8 * 24 * 3600),
                false,
                now),
            DeadlineUrgency::kSoon);
        EXPECT_EQ(
            ClassifyDeadlineUrgency(
                format(31 * 24 * 3600),
                false,
                now),
            DeadlineUrgency::kNormal);

        // A date-only deadline expires at the end of its Beijing day, so a
        // noon-of-that-day instant that is already past is still urgent, not
        // expired.
        EXPECT_EQ(
            ClassifyDeadlineUrgency(
                format(-4 * 3600),
                true,
                now),
            DeadlineUrgency::kUrgent);

        // Same input twice -> same verdict (determinism).
        EXPECT_EQ(
            ClassifyDeadlineUrgency(
                format(6 * 24 * 3600),
                false,
                now),
            ClassifyDeadlineUrgency(
                format(6 * 24 * 3600),
                false,
                now));
    }

    TEST(MatchExplanationTest, DealBreakersAreDataWithNullNeverExcluded)
    {
        const auto now =
            std::chrono::system_clock::from_time_t(1800000000);

        JobSummary job;
        job.educationRequired = 4;
        job.jobNature = 2;

        UserProfile profile;
        profile.educationLevel = 3;
        profile.campusStatus = 3;
        profile.salaryMin = 10;
        profile.salaryMax = 20;

        job.salaryMin = 30;
        job.salaryMax = 40;

        const auto breakers =
            EvaluateMatchDealBreakers(job, profile, now);

        ASSERT_EQ(breakers.size(), 3U);
        EXPECT_EQ(breakers[0].code, "education_unmet");
        EXPECT_EQ(breakers[1].code, "campus_incompatible");
        EXPECT_EQ(breakers[2].code, "salary_no_intersection");
        for (const MatchDealBreaker &breaker : breakers)
        {
            EXPECT_FALSE(breaker.text.empty());
        }

        // NULL job requirement -> never excluded.
        JobSummary noRequirement = job;
        noRequirement.educationRequired.reset();
        noRequirement.jobNature.reset();
        noRequirement.salaryMin.reset();
        noRequirement.salaryMax.reset();
        EXPECT_TRUE(
            EvaluateMatchDealBreakers(
                noRequirement,
                profile,
                now)
                .empty());

        // NULL profile field -> no penalty, and the missing side is reported as
        // an advisory reason instead.
        UserProfile emptyProfile;
        EXPECT_TRUE(
            EvaluateMatchDealBreakers(job, emptyProfile, now)
                .empty());
        EXPECT_TRUE(
            EvaluateMatchDealBreakers(job, std::nullopt, now)
                .empty());

        const auto advisories =
            ProjectMatchReasons(
                job,
                emptyProfile,
                {},
                {},
                now);

        bool educationAdvisory = false;
        bool campusAdvisory = false;
        bool salaryAdvisory = false;

        for (const MatchReason &reason : advisories)
        {
            educationAdvisory =
                educationAdvisory ||
                reason.code == "education_unknown";
            campusAdvisory =
                campusAdvisory ||
                reason.code == "campus_unknown";
            salaryAdvisory =
                salaryAdvisory ||
                reason.code == "salary_unknown";
            EXPECT_EQ(reason.evidence, "profile");
        }

        EXPECT_TRUE(educationAdvisory);
        EXPECT_TRUE(campusAdvisory);
        EXPECT_TRUE(salaryAdvisory);

        // An expired deadline is a deal-breaker; a NULL one never is.
        JobSummary expired = noRequirement;
        expired.deadline = "2000-01-01 00:00:00";
        const auto expiredBreakers =
            EvaluateMatchDealBreakers(expired, profile, now);
        ASSERT_EQ(expiredBreakers.size(), 1U);
        EXPECT_EQ(expiredBreakers[0].code, "deadline_expired");
    }

    TEST(MatchExplanationTest, ReasonsCiteDeliveredFactsOnly)
    {
        const auto now =
            std::chrono::system_clock::from_time_t(1800000000);

        JobSummary job;
        job.location = "深圳";
        // Well beyond `now` (1800000000 = 2027-01-15) so the reason is a
        // deadline-urgency statement rather than an expired veto.
        job.deadline = "2028-01-01 00:00:00";

        UserProfile profile;
        profile.location = "上海";

        const auto reasons =
            ProjectMatchReasons(
                job,
                profile,
                {"C++"},
                {"Redis"},
                now);

        std::vector<std::string> codes;

        for (const MatchReason &reason : reasons)
        {
            codes.push_back(reason.code);
            EXPECT_FALSE(reason.text.empty());
            EXPECT_FALSE(reason.evidence.empty());
        }

        EXPECT_NE(
            std::find(codes.begin(), codes.end(), "skills_matched"),
            codes.end());
        EXPECT_NE(
            std::find(codes.begin(), codes.end(), "skills_missing"),
            codes.end());
        EXPECT_NE(
            std::find(codes.begin(), codes.end(), "location_mismatch"),
            codes.end());

        bool deadlineReason = false;

        for (const MatchReason &reason : reasons)
        {
            if (reason.evidence == "deadline")
            {
                deadlineReason = true;
                EXPECT_NE(reason.code, "deadline_expired");
            }
        }

        EXPECT_TRUE(deadlineReason);

        // No facts, no invented reasons: an empty profile/job yields nothing but
        // the deadline-unknown case (which is silent by contract).
        JobSummary emptyJob;
        const auto emptyReasons =
            ProjectMatchReasons(
                emptyJob,
                std::nullopt,
                {},
                {},
                now);
        EXPECT_TRUE(emptyReasons.empty());

        // Determinism: identical inputs -> identical projections.
        EXPECT_EQ(
            reasons.size(),
            ProjectMatchReasons(
                job,
                profile,
                {"C++"},
                {"Redis"},
                now)
                .size());
    }

} // namespace jobpilot::service
