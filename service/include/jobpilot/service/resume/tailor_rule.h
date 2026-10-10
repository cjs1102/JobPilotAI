#pragma once

#include <string>
#include <vector>

#include "jobpilot/service/job/job_dao_interface.h"
#include "jobpilot/service/user/user_dao_interface.h"

namespace jobpilot::service
{
    // 0015 Phase D step D2 (owner decision 3): the deterministic, ATS-aware
    // Chinese draft generator. Pure by contract: no I/O, no clock, no
    // randomness, no LLM, no AI key.
    inline constexpr const char *kTailorGenerator = "rule_v1";
    inline constexpr const char *kTailorSourceVersion = "tailor_rule_v1";

    // Provenance prefixes. Every asserted fact must cite exactly one of these.
    inline constexpr const char *kTailorSourceProfile = "profile:";
    inline constexpr const char *kTailorSourceSkill = "skill:";
    inline constexpr const char *kTailorSourceRequirement = "requirement:";
    inline constexpr const char *kTailorSourceJob = "job:";
    inline constexpr const char *kTailorSourceResumeContent =
        "resume_version:content";

    // One asserted fact plus the source it is provenanced to.
    struct TailorFact
    {
        std::string text;
        std::string source;
    };

    // A draft never becomes a claim about the candidate beyond its sources:
    // anything the sources do not carry becomes a 【请填写：...】 slot.
    struct TailorDraft
    {
        std::string content;
        std::vector<TailorFact> facts;
        std::vector<std::string> placeholders;
        std::string generator{kTailorGenerator};
        std::string sourceVersion{kTailorSourceVersion};
    };

    // Deterministic draft from the candidate's profile + chosen resume-version
    // text + the job posting. Same inputs -> byte-identical output.
    TailorDraft BuildTailorDraft(
        const std::string &candidateName,
        const std::string &candidateEmail,
        const UserProfile &profile,
        const std::vector<UserSkill> &skills,
        const std::string &resumeContent,
        const JobSummary &job,
        const std::vector<JobRequirement> &requirements);
}
