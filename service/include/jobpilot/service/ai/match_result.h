#pragma once

#include <optional>
#include <string>
#include <vector>

namespace jobpilot::service
{
    // One deterministic, evidence-tagged match statement (ExecPlan 0013 5.5).
    struct MatchEvidenceItem
    {
        std::string text;
        std::string evidence;
    };

    // Deterministic keyword presence for one required skill in the resume text.
    struct MatchResumeEvidence
    {
        std::string skill;
        bool mentioned = false;
    };

    // match_rule_v1 component scores. The ranking-only recency component is
    // intentionally absent: recency is not match quality.
    struct MatchBreakdown
    {
        std::optional<double> skill;
        std::optional<double> title;
        std::optional<double> salary;
        std::optional<double> location;
    };

    struct MatchResult
    {
        std::string scoreVersion{"match_rule_v1"};
        double score = 0.0;
        std::string source{"rule"};
        MatchBreakdown breakdown;
        std::vector<std::string> matchedSkills;
        std::vector<std::string> missingSkills;
        std::vector<MatchEvidenceItem> strengths;
        std::vector<MatchEvidenceItem> gaps;
        std::vector<MatchResumeEvidence> resumeEvidence;
    };

} // namespace jobpilot::service
