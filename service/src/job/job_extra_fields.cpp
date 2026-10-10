#include "jobpilot/service/job/job_extra_fields.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include "jobpilot/service/job/source_observation.h"

namespace jobpilot::service
{
    namespace
    {
        constexpr std::int64_t kSecondsPerDay = 24 * 60 * 60;
        constexpr int kMinYear = 1000;
        constexpr int kMaxYear = 9999;

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

        int ParseTwoDigits(const std::string &value, std::size_t index)
        {
            return (value[index] - '0') * 10 +
                   (value[index + 1] - '0');
        }

        int ParseFourDigits(const std::string &value, std::size_t index)
        {
            return (value[index] - '0') * 1000 +
                   (value[index + 1] - '0') * 100 +
                   (value[index + 2] - '0') * 10 +
                   (value[index + 3] - '0');
        }

        std::string Pad(int value, std::size_t width)
        {
            std::string text = std::to_string(value);

            while (text.size() < width)
            {
                text.insert(text.begin(), '0');
            }

            return text;
        }

        std::string FormatDbTimestamp(
            int year,
            int month,
            int day,
            int hour,
            int minute,
            int second)
        {
            return Pad(year, 4) + "-" + Pad(month, 2) + "-" +
                   Pad(day, 2) + " " + Pad(hour, 2) + ":" +
                   Pad(minute, 2) + ":" + Pad(second, 2);
        }

        // Days since 1970-01-01 (Howard Hinnant's days_from_civil).
        std::int64_t DaysFromCivil(int year, int month, int day)
        {
            year -= month <= 2 ? 1 : 0;

            const std::int64_t era =
                (year >= 0 ? year : year - 399) / 400;
            const std::int64_t yearOfEra =
                year - era * 400;
            const std::int64_t dayOfYear =
                (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 +
                day - 1;
            const std::int64_t dayOfEra =
                yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 +
                dayOfYear;

            return era * 146097 + dayOfEra - 719468;
        }

        // Inverse of DaysFromCivil (civil_from_days).
        void CivilFromDays(
            std::int64_t days,
            int *year,
            int *month,
            int *day)
        {
            days += 719468;

            const std::int64_t era =
                (days >= 0 ? days : days - 146096) / 146097;
            const std::int64_t dayOfEra = days - era * 146097;
            const std::int64_t yearOfEra =
                (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 -
                 dayOfEra / 146096) /
                365;
            const std::int64_t absoluteYear = yearOfEra + era * 400;
            const std::int64_t dayOfYear =
                dayOfEra - (365 * yearOfEra + yearOfEra / 4 -
                            yearOfEra / 100);
            const std::int64_t monthPrime =
                (5 * dayOfYear + 2) / 153;
            const int computedMonth = static_cast<int>(
                monthPrime + (monthPrime < 10 ? 3 : -9));

            *day = static_cast<int>(
                dayOfYear - (153 * monthPrime + 2) / 5 + 1);
            *month = computedMonth;
            *year = static_cast<int>(
                absoluteYear + (computedMonth <= 2 ? 1 : 0));
        }

        bool SplitDbTimestamp(
            const std::string &value,
            int *year,
            int *month,
            int *day,
            int *hour,
            int *minute,
            int *second)
        {
            if (!IsValidDbTimestamp(value))
            {
                return false;
            }

            *year = ParseFourDigits(value, 0);
            *month = ParseTwoDigits(value, 5);
            *day = ParseTwoDigits(value, 8);
            *hour = ParseTwoDigits(value, 11);
            *minute = ParseTwoDigits(value, 14);
            *second = ParseTwoDigits(value, 17);
            return true;
        }
    } // namespace

    bool ValidateJobExtraFields(const JobImportData &job)
    {
        if (job.educationRequired.has_value() &&
            (job.educationRequired.value() < kEducationRequiredMin ||
             job.educationRequired.value() > kEducationRequiredMax))
        {
            return false;
        }

        if (job.jobNature.has_value() &&
            (job.jobNature.value() < kJobNatureMin ||
             job.jobNature.value() > kJobNatureMax))
        {
            return false;
        }

        for (const std::optional<std::uint32_t> &years :
             {job.experienceYearsMin, job.experienceYearsMax})
        {
            if (years.has_value() &&
                years.value() > kExperienceYearsMax)
            {
                return false;
            }
        }

        if (job.experienceYearsMin.has_value() &&
            job.experienceYearsMax.has_value() &&
            job.experienceYearsMin.value() >
                job.experienceYearsMax.value())
        {
            return false;
        }

        if (job.deadline.has_value() &&
            !IsValidDbTimestamp(job.deadline.value()))
        {
            return false;
        }

        if (job.deadlineDateOnly.has_value())
        {
            if (!job.deadline.has_value())
            {
                return false;
            }

            if (job.deadlineDateOnly.value() &&
                !IsDateOnlyStoredDeadline(job.deadline.value()))
            {
                return false;
            }
        }

        return true;
    }

    void NormalizeJobExtraFields(JobImportData *job)
    {
        if (job == nullptr)
        {
            return;
        }

        if (!job->deadline.has_value())
        {
            // An explicit flag without a deadline is an inconsistent request;
            // leave it for validation to reject instead of silently dropping
            // a client-supplied key (docs/03 section 9.3).
            return;
        }

        if (!job->deadlineDateOnly.has_value())
        {
            // An omitted flag next to a present deadline means the source gave
            // an explicit instant (docs/03 section 9.1).
            job->deadlineDateOnly = false;
        }
    }

    std::optional<std::string> AddSecondsToDbTimestamp(
        const std::string &value,
        std::int64_t seconds)
    {
        int year = 0;
        int month = 0;
        int day = 0;
        int hour = 0;
        int minute = 0;
        int second = 0;

        if (!SplitDbTimestamp(
                value,
                &year,
                &month,
                &day,
                &hour,
                &minute,
                &second))
        {
            return std::nullopt;
        }

        const std::int64_t total =
            DaysFromCivil(year, month, day) * kSecondsPerDay +
            hour * 3600 + minute * 60 + second + seconds;

        std::int64_t days = total / kSecondsPerDay;
        std::int64_t remainder = total % kSecondsPerDay;

        if (remainder < 0)
        {
            remainder += kSecondsPerDay;
            --days;
        }

        int newYear = 0;
        int newMonth = 0;
        int newDay = 0;
        CivilFromDays(days, &newYear, &newMonth, &newDay);

        if (newYear < kMinYear || newYear > kMaxYear)
        {
            return std::nullopt;
        }

        return FormatDbTimestamp(
            newYear,
            newMonth,
            newDay,
            static_cast<int>(remainder / 3600),
            static_cast<int>((remainder % 3600) / 60),
            static_cast<int>(remainder % 60));
    }

    bool IsDateOnlyStoredDeadline(const std::string &utcValue)
    {
        if (!IsValidDbTimestamp(utcValue))
        {
            return false;
        }

        return utcValue.compare(11, 8, kDeadlineDateOnlyUtcTime) == 0;
    }

    std::optional<std::string> MakeDateOnlyStoredDeadline(
        const std::string &beijingDate)
    {
        if (beijingDate.size() != 10 ||
            beijingDate[4] != '-' ||
            beijingDate[7] != '-')
        {
            return std::nullopt;
        }

        for (const std::size_t index : {0U, 1U, 2U, 3U, 5U, 6U, 8U, 9U})
        {
            const char character = beijingDate[index];

            if (character < '0' || character > '9')
            {
                return std::nullopt;
            }
        }

        const int year = ParseFourDigits(beijingDate, 0);
        const int month = ParseTwoDigits(beijingDate, 5);
        const int day = ParseTwoDigits(beijingDate, 8);

        if (year < kMinYear || year > kMaxYear ||
            month < 1 || month > 12 ||
            day < 1 || day > DaysInMonth(year, month))
        {
            return std::nullopt;
        }

        const std::string beijingNoon =
            FormatDbTimestamp(
                year,
                month,
                day,
                kDeadlineDateOnlyBeijingHour,
                0,
                0);

        return AddSecondsToDbTimestamp(
            beijingNoon,
            -kBeijingUtcOffsetSeconds);
    }

    std::optional<std::string> DeadlineToBeijingLocal(
        const std::string &utcValue)
    {
        return AddSecondsToDbTimestamp(
            utcValue,
            kBeijingUtcOffsetSeconds);
    }

    std::optional<std::string> DeadlineToBeijingDate(
        const std::string &utcValue)
    {
        const std::optional<std::string> local =
            DeadlineToBeijingLocal(utcValue);

        if (!local.has_value())
        {
            return std::nullopt;
        }

        return local->substr(0, 10);
    }

} // namespace jobpilot::service
