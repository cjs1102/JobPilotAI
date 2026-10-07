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
    };

    bool NormalizeJobSearchFilter(
        const JobSearchFilter &input,
        JobSearchFilter *normalized);

} // namespace jobpilot::service
