#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "jobpilot/service/job/job_dao_interface.h"
#include "jobpilot/service/job/job_search_filter.h"
#include "jobpilot/service/user/user_dao_interface.h"

namespace jobpilot::service
{
    struct RankingBreakdown
    {
        std::optional<double> skill;
        std::optional<double> title;
        std::optional<double> salary;
        std::optional<double> location;
        std::optional<double> firstSeen;
    };

    // rule_v1 component weights. Exported so 0013 match scoring can reuse the
    // same primitives instead of duplicating the numbers; match_rule_v1 drops
    // the ranking-only recency component and renormalizes.
    inline constexpr double kRuleV1SkillWeight = 0.55;
    inline constexpr double kRuleV1TitleWeight = 0.15;
    inline constexpr double kRuleV1SalaryWeight = 0.15;
    inline constexpr double kRuleV1LocationWeight = 0.10;
    inline constexpr double kRuleV1FirstSeenWeight = 0.05;
    inline constexpr double kRuleV1FirstSeenWindowDays = 30.0;

    struct RankedJob
    {
        JobSummary job;
        double score = 0.0;
        std::string scoreVersion{"rule_v1"};
        RankingBreakdown breakdown;
        std::vector<std::string> matchedSkills;
        std::vector<std::string> missingSkills;
    };

    RankedJob ScoreJob(
        const JobSummary &job,
        const std::vector<JobRequirement> &requirements,
        const std::optional<UserProfile> &profile,
        const std::vector<UserSkill> &skills,
        const JobSearchFilter &filter,
        std::chrono::system_clock::time_point now);

} // namespace jobpilot::service
