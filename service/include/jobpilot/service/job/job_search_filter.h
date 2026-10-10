#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace jobpilot::service
{
    // 0015A: whitelisted public-search sort orders. kDeadlineAsc is index-backed
    // (docs/03 section 9.2), which requires the consistent direction
    // "deadline ASC, id ASC" and therefore places NULL (unknown) deadlines
    // first; a NULLs-last order needs an expression and forfeits that index.
    enum class JobSort
    {
        kCreatedDesc = 0,
        kDeadlineAsc = 1,
        kEducationAsc = 2
    };

    struct JobSearchFilter
    {
        std::optional<std::string> keyword;
        std::optional<std::string> location;
        std::optional<std::uint32_t> salaryMin;
        std::optional<std::uint32_t> salaryMax;
        std::optional<std::string> skill;

        // 0012 freshness visibility (ExecPlan 0012 section 11). Closed jobs are
        // always excluded; stale jobs are excluded unless includeStale is set.
        // postedWithinDays filters on the source publication time and excludes
        // jobs without a reliable published_at.
        bool includeStale = false;
        std::optional<std::uint32_t> postedWithinDays{};

        // 0015A focus fields (docs/04 section 5.1). Defaults keep every existing
        // aggregate initialisation valid unchanged.
        std::optional<std::uint32_t> educationMax{};
        std::optional<std::uint32_t> jobNature{};
        std::optional<std::string> deadlineBefore{};
        std::optional<std::string> deadlineAfter{};
        bool openOnly = false;
        JobSort sort = JobSort::kCreatedDesc;

        // 0015A public visibility: public search/recommended return only
        // review_status = 0 rows unless this switch is enabled (ExecPlan 0015A
        // sections 5.1/6.4). Default false keeps needs_review rows hidden; the
        // switch is configuration-driven at the controller boundary and never
        // changes data.
        bool includeNeedsReview = false;

        // 0015 B4 (additive, default off). `withMatch` asks the public list to
        // carry the deterministic explanation per row (score / score_version /
        // matched_skills / missing_skills / reasons / deal_breakers /
        // deadline_urgency); absent, the payload is byte-identical to today.
        // `strict` is the opt-in exclusion of deal-breaker rows (B3).
        bool withMatch = false;
        bool strict = false;
    };

    bool NormalizeJobSearchFilter(
        const JobSearchFilter &input,
        JobSearchFilter *normalized);

} // namespace jobpilot::service
