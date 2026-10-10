#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "jobpilot/service/job/job_dao_interface.h"
#include "jobpilot/service/user/user_dao_interface.h"

namespace jobpilot::service
{
    // 0015 Phase B step B1: deterministic, evidence-backed match inputs.
    //
    // Decision 5 (owner): the `rule_v1` weights are NOT touched. Education,
    // campus and salary compatibility are computed here as *data* — verdicts,
    // advisory reasons and vetoes — and never re-weight the public score.
    // Nothing in this header excludes a row; exclusion is B3's explicit
    // `strict=true` opt-in.

    // Deadline urgency is presentation metadata only (B3 contract): it never
    // closes a job, never writes freshness and never changes `review_status`.
    enum class DeadlineUrgency
    {
        kExpired,
        kUrgent,
        kSoon,
        kNormal,
        kUnknown
    };

    const char *ToString(DeadlineUrgency urgency);

    // Stable evidence-backed statements (`code` machine-stable, `evidence` one
    // of the match_rule.h tags, plus the new `deadline` tag).
    struct MatchReason
    {
        std::string code;
        std::string text;
        std::string evidence;
    };

    // A deterministic veto returned as data. `strict=true` (opt-in, B3) is the
    // only path that turns these into an exclusion.
    struct MatchDealBreaker
    {
        std::string code;
        std::string text;
    };

    inline constexpr const char *kDealBreakerEducationUnmet =
        "education_unmet";
    inline constexpr const char *kDealBreakerCampusIncompatible =
        "campus_incompatible";
    inline constexpr const char *kDealBreakerSalaryNoIntersection =
        "salary_no_intersection";
    inline constexpr const char *kDealBreakerDeadlineExpired =
        "deadline_expired";

    inline constexpr const char *kEvidenceDeadline = "deadline";

    inline constexpr int kDeadlineUrgentDays = 7;
    inline constexpr int kDeadlineSoonDays = 30;

    // Stored UTC instant + `deadline_date_only` -> presentation urgency.
    // A NULL deadline is `unknown` and is NEVER `expired` (B3/F13 invariant).
    DeadlineUrgency ClassifyDeadlineUrgency(
        const std::optional<std::string> &deadline,
        const std::optional<bool> &deadlineDateOnly,
        std::chrono::system_clock::time_point now);

    // NULL semantics (docs/03 section 10.2 + the B1 acceptance cases):
    //   * a NULL job requirement (or a NULL profile field) never produces a
    //     deal-breaker and never penalizes the candidate;
    //   * the missing side is surfaced as an advisory reason instead.
    std::vector<MatchDealBreaker> EvaluateMatchDealBreakers(
        const JobSummary &job,
        const std::optional<UserProfile> &profile,
        std::chrono::system_clock::time_point now);

    // Deterministic reason projection for the explainable payload. Only
    // delivered facts are cited; nothing is inferred.
    std::vector<MatchReason> ProjectMatchReasons(
        const JobSummary &job,
        const std::optional<UserProfile> &profile,
        const std::vector<std::string> &matchedSkills,
        const std::vector<std::string> &missingSkills,
        std::chrono::system_clock::time_point now);
}
