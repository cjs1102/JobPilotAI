#include "jobpilot/service/job/source_observation.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <set>
#include <string>

#include "jobpilot/service/job/job_extra_fields.h"

namespace jobpilot::service
{
    namespace
    {
        // Field bounds mirror the docs/03 column widths and the normalized-job
        // rules enforced by JobIngestionService for the manual import path.
        constexpr std::size_t kMaxSourceBytes = 32;
        constexpr std::size_t kMaxSourceScopeBytes = 128;
        constexpr std::size_t kMaxCompanyBytes = 128;
        constexpr std::size_t kMaxTitleBytes = 128;
        constexpr std::size_t kMaxLocationBytes = 64;
        constexpr std::size_t kMaxCurrencyBytes = 8;
        constexpr std::size_t kMaxDescriptionBytes = 65535;
        constexpr std::size_t kMaxSourceUrlBytes = 255;
        constexpr std::size_t kMaxExternalIdBytes = 255;
        constexpr std::size_t kMaxSkillBytes = 64;

        constexpr std::size_t kDbTimestampLength = 19;
        constexpr int kMinYear = 1000;
        constexpr int kMaxYear = 9999;

        std::string FoldAsciiCase(const std::string &value)
        {
            std::string folded = value;

            std::transform(
                folded.begin(),
                folded.end(),
                folded.begin(),
                [](unsigned char character)
                {
                    return static_cast<char>(
                        std::tolower(character));
                });

            return folded;
        }

        bool HasValueWithin(
            const std::string &value,
            std::size_t maxBytes,
            bool required)
        {
            if (required && value.empty())
            {
                return false;
            }

            return value.size() <= maxBytes;
        }

        bool IsLeapYear(int year)
        {
            return (year % 4 == 0 && year % 100 != 0) ||
                   year % 400 == 0;
        }

        int DaysInMonth(int year, int month)
        {
            switch (month)
            {
            case 1:
            case 3:
            case 5:
            case 7:
            case 8:
            case 10:
            case 12:
                return 31;
            case 4:
            case 6:
            case 9:
            case 11:
                return 30;
            case 2:
                return IsLeapYear(year) ? 29 : 28;
            default:
                return 0;
            }
        }

        bool IsDigitAt(
            const std::string &value,
            std::size_t index)
        {
            return std::isdigit(
                       static_cast<unsigned char>(value[index])) != 0;
        }

        int ParseTwoDigits(
            const std::string &value,
            std::size_t index)
        {
            return (value[index] - '0') * 10 +
                   (value[index + 1] - '0');
        }

        int ParseFourDigits(
            const std::string &value,
            std::size_t index)
        {
            return (value[index] - '0') * 1000 +
                   (value[index + 1] - '0') * 100 +
                   (value[index + 2] - '0') * 10 +
                   (value[index + 3] - '0');
        }

        bool ValidateRequirements(
            const std::vector<JobRequirementImportData> &requirements)
        {
            std::set<std::string> skills;

            for (const JobRequirementImportData &requirement :
                 requirements)
            {
                if (!HasValueWithin(
                        requirement.skill,
                        kMaxSkillBytes,
                        true) ||
                    requirement.level < 1 ||
                    requirement.level > 3)
                {
                    return false;
                }

                if (!skills.insert(
                         FoldAsciiCase(requirement.skill))
                         .second)
                {
                    return false;
                }
            }

            return true;
        }

        bool ValidateObservationJob(
            const SourceObservationJob &item,
            const std::string &source)
        {
            const JobImportData &job = item.job;

            // 0015A option B: the quality gate classifies a missing employer as
            // needs_review, so only the byte cap is enforced structurally.
            if (!HasValueWithin(
                    job.company,
                    kMaxCompanyBytes,
                    false) ||
                !HasValueWithin(
                    job.title,
                    kMaxTitleBytes,
                    true) ||
                !HasValueWithin(
                    job.source,
                    kMaxSourceBytes,
                    true) ||
                !HasValueWithin(
                    job.sourceUrl,
                    kMaxSourceUrlBytes,
                    true) ||
                !HasValueWithin(
                    job.salaryCurrency,
                    kMaxCurrencyBytes,
                    true))
            {
                return false;
            }

            // A batch belongs to exactly one source; a mismatched per-job
            // source would silently write the wrong identity.
            if (job.source != source)
            {
                return false;
            }

            if (job.location.has_value() &&
                !HasValueWithin(
                    job.location.value(),
                    kMaxLocationBytes,
                    false))
            {
                return false;
            }

            if (job.description.has_value() &&
                !HasValueWithin(
                    job.description.value(),
                    kMaxDescriptionBytes,
                    false))
            {
                return false;
            }

            if (job.salaryMin.has_value() &&
                job.salaryMax.has_value() &&
                job.salaryMax.value() <
                    job.salaryMin.value())
            {
                return false;
            }

            if (item.externalId.has_value() &&
                !HasValueWithin(
                    item.externalId.value(),
                    kMaxExternalIdBytes,
                    false))
            {
                return false;
            }

            if (item.publishedAt.has_value() &&
                !IsValidDbTimestamp(item.publishedAt.value()))
            {
                return false;
            }

            if (!ValidateJobExtraFields(job))
            {
                return false;
            }

            return ValidateRequirements(item.requirements);
        }
    } // namespace

    const char *ToString(SourceStatus status)
    {
        switch (status)
        {
        case SourceStatus::kUnknown:
            return "unknown";
        case SourceStatus::kOk:
            return "ok";
        case SourceStatus::kMissing:
            return "missing";
        case SourceStatus::kClosed:
            return "closed";
        }

        return "unknown";
    }

    bool ParseSourceStatus(
        const std::string &value,
        SourceStatus *status)
    {
        if (status == nullptr)
        {
            return false;
        }

        if (value == "unknown")
        {
            *status = SourceStatus::kUnknown;
            return true;
        }

        if (value == "ok")
        {
            *status = SourceStatus::kOk;
            return true;
        }

        if (value == "missing")
        {
            *status = SourceStatus::kMissing;
            return true;
        }

        if (value == "closed")
        {
            *status = SourceStatus::kClosed;
            return true;
        }

        return false;
    }

    const char *ToString(FreshnessState state)
    {
        switch (state)
        {
        case FreshnessState::kFresh:
            return "fresh";
        case FreshnessState::kAging:
            return "aging";
        case FreshnessState::kStale:
            return "stale";
        case FreshnessState::kClosed:
            return "closed";
        }

        return "fresh";
    }

    bool IsSupportedObservationSource(
        const std::string &source)
    {
        return source == "greenhouse" ||
               source == "lever" ||
               source == "ashby" ||
               source == "nowcoder";  // registry-guard: source-id
    }

    bool IsValidDbTimestamp(
        const std::string &value)
    {
        if (value.size() != kDbTimestampLength)
        {
            return false;
        }

        if (value[4] != '-' ||
            value[7] != '-' ||
            value[10] != ' ' ||
            value[13] != ':' ||
            value[16] != ':')
        {
            return false;
        }

        const std::size_t digitIndexes[] = {
            0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18};

        for (const std::size_t index : digitIndexes)
        {
            if (!IsDigitAt(value, index))
            {
                return false;
            }
        }

        const int year = ParseFourDigits(value, 0);
        const int month = ParseTwoDigits(value, 5);
        const int day = ParseTwoDigits(value, 8);
        const int hour = ParseTwoDigits(value, 11);
        const int minute = ParseTwoDigits(value, 14);
        const int second = ParseTwoDigits(value, 17);

        if (year < kMinYear || year > kMaxYear)
        {
            return false;
        }

        if (month < 1 || month > 12)
        {
            return false;
        }

        if (day < 1 || day > DaysInMonth(year, month))
        {
            return false;
        }

        return hour >= 0 && hour <= 23 &&
               minute >= 0 && minute <= 59 &&
               second >= 0 && second <= 59;
    }

    bool ValidateSourceObservation(
        const SourceObservation &observation)
    {
        if (!HasValueWithin(
                observation.source,
                kMaxSourceBytes,
                true) ||
            !IsSupportedObservationSource(observation.source))
        {
            return false;
        }

        if (!HasValueWithin(
                observation.sourceScope,
                kMaxSourceScopeBytes,
                true))
        {
            return false;
        }

        if (!IsValidDbTimestamp(observation.observedAt))
        {
            return false;
        }

        const std::size_t count = observation.jobs.size();

        if (count > kMaxObservationBatchSize)
        {
            return false;
        }

        if (!observation.complete && count == 0)
        {
            return false;
        }

        std::set<std::string> sourceUrls;

        for (const SourceObservationJob &item : observation.jobs)
        {
            if (!ValidateObservationJob(
                    item,
                    observation.source))
            {
                return false;
            }

            if (!sourceUrls.insert(
                     FoldAsciiCase(item.job.sourceUrl))
                     .second)
            {
                return false;
            }
        }

        return true;
    }

    bool ShouldCloseMissingJob(
        std::int32_t missingCount,
        const std::optional<std::int64_t> &secondsSinceLastSeen,
        const MissingPolicy &policy)
    {
        if (missingCount >= policy.closeAfterMisses)
        {
            return true;
        }

        return secondsSinceLastSeen.has_value() &&
               secondsSinceLastSeen.value() > policy.graceSeconds;
    }

    std::optional<FreshnessState> ClassifyFreshness(
        bool closed,
        const std::optional<std::int64_t> &ageSeconds)
    {
        if (closed)
        {
            return FreshnessState::kClosed;
        }

        if (!ageSeconds.has_value())
        {
            return std::nullopt;
        }

        const std::int64_t age =
            std::max<std::int64_t>(ageSeconds.value(), 0);

        if (age <= kFreshMaxAgeSeconds)
        {
            return FreshnessState::kFresh;
        }

        if (age <= kAgingMaxAgeSeconds)
        {
            return FreshnessState::kAging;
        }

        return FreshnessState::kStale;
    }

} // namespace jobpilot::service
