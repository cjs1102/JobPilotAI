#include "jobpilot/service/job/job_search_filter.h"

#include <cctype>
#include <optional>
#include <string>
#include <utility>

namespace jobpilot::service
{
    namespace
    {
        constexpr std::size_t kMaxKeywordBytes = 128;
        constexpr std::size_t kMaxLocationBytes = 64;
        constexpr std::size_t kMaxSkillBytes = 64;

        std::string Trim(
            const std::string &value)
        {
            std::size_t begin = 0;
            std::size_t end = value.size();

            while (begin < end &&
                   std::isspace(
                       static_cast<unsigned char>(
                           value[begin])))
            {
                ++begin;
            }

            while (end > begin &&
                   std::isspace(
                       static_cast<unsigned char>(
                           value[end - 1])))
            {
                --end;
            }

            return value.substr(
                begin,
                end - begin);
        }

        bool NormalizeOptional(
            const std::optional<std::string> &value,
            std::size_t maxBytes,
            std::optional<std::string> *normalized)
        {
            if (!value.has_value())
            {
                normalized->reset();
                return true;
            }

            const std::string trimmed =
                Trim(value.value());

            if (trimmed.empty())
            {
                normalized->reset();
                return true;
            }

            if (trimmed.size() > maxBytes)
            {
                return false;
            }

            *normalized = trimmed;
            return true;
        }
    } // namespace

    bool NormalizeJobSearchFilter(
        const JobSearchFilter &input,
        JobSearchFilter *normalized)
    {
        if (normalized == nullptr)
        {
            return false;
        }

        JobSearchFilter result = input;

        if (!NormalizeOptional(
                input.keyword,
                kMaxKeywordBytes,
                &result.keyword) ||
            !NormalizeOptional(
                input.location,
                kMaxLocationBytes,
                &result.location) ||
            !NormalizeOptional(
                input.skill,
                kMaxSkillBytes,
                &result.skill))
        {
            return false;
        }

        if (result.salaryMin.has_value() &&
            result.salaryMax.has_value() &&
            result.salaryMax.value() <
                result.salaryMin.value())
        {
            return false;
        }

        *normalized = std::move(result);
        return true;
    }

} // namespace jobpilot::service
