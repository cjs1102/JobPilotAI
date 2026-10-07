#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace jobpilot::service
{
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
    };

    bool NormalizeJobSearchFilter(
        const JobSearchFilter &input,
        JobSearchFilter *normalized);

} // namespace jobpilot::service
