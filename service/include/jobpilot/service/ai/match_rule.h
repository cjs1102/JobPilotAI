#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "jobpilot/service/ai/match_result.h"
#include "jobpilot/service/job/job_dao_interface.h"
#include "jobpilot/service/user/user_dao_interface.h"

namespace jobpilot::service
{
    // Evidence tags used by the result contract (ExecPlan 0013 5.5).
    inline constexpr const char *kEvidenceSkills = "skills";
    inline constexpr const char *kEvidenceJobRequirements = "job_requirements";
    inline constexpr const char *kEvidenceProfile = "profile";
    inline constexpr const char *kEvidenceResumeText = "resume_text";

    // Deterministic match score. Reuses the rule_v1 component primitives but
    // drops the ranking-only recency component and renormalizes over the
    // components that are actually available.
    MatchResult ScoreMatch(
        const JobSummary &job,
        const std::vector<JobRequirement> &requirements,
        const std::optional<UserProfile> &profile,
        const std::vector<UserSkill> &skills,
        const std::string &resumeContent,
        std::chrono::system_clock::time_point now);

    // Word-boundary aware, case-insensitive presence check. Presence is
    // evidence, not proof of a claimed skill.
    bool ResumeMentionsSkill(
        const std::string &resumeContent,
        const std::string &skill);

} // namespace jobpilot::service
