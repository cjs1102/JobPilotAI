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
        constexpr std::uint32_t kMaxPostedWithinDays = 3650;
        constexpr std::uint32_t kMaxEducation = 5;
        constexpr std::uint32_t kMaxJobNature = 3;

        // 0015A: deadline range bounds arrive as "YYYY-MM-DD HH:MM:SS" (the
        // format the database contract uses, docs/03 section 9.1).
        bool IsDbTimestamp(
            const std::string &value)
        {
            if (value.size() != 19 ||
                value[4] != '-' ||
                value[7] != '-' ||
                value[10] != ' ' ||
                value[13] != ':' ||
                value[16] != ':')
            {
                return false;
            }

            for (std::size_t index = 0;
                 index < value.size();
                 ++index)
            {
                if (index == 4 || index == 7 ||
                    index == 10 || index == 13 ||
                    index == 16)
                {
                    continue;
                }

                if (!std::isdigit(
                        static_cast<unsigned char>(
                            value[index])))
                {
                    return false;
                }
            }

            return true;
        }

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

        if (result.postedWithinDays.has_value() &&
            (result.postedWithinDays.value() < 1 ||
             result.postedWithinDays.value() >
                 kMaxPostedWithinDays))
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

        // 0015A focus-field validation (docs/04 section 5.1).
        if ((result.educationMax.has_value() &&
             (result.educationMax.value() < 1 ||
              result.educationMax.value() > kMaxEducation)) ||
            (result.jobNature.has_value() &&
             (result.jobNature.value() < 1 ||
              result.jobNature.value() > kMaxJobNature)))
        {
            return false;
        }

        if (result.deadlineBefore.has_value() &&
            !IsDbTimestamp(result.deadlineBefore.value()))
        {
            return false;
        }

        if (result.deadlineAfter.has_value() &&
            !IsDbTimestamp(result.deadlineAfter.value()))
        {
            return false;
        }

        if (result.deadlineBefore.has_value() &&
            result.deadlineAfter.has_value() &&
            result.deadlineBefore.value() <
                result.deadlineAfter.value())
        {
            return false;
        }

        *normalized = std::move(result);
        return true;
    }

} // namespace jobpilot::service
