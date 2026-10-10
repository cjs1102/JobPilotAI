#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "jobpilot/service/job/job_ingestion_dao_interface.h"

namespace jobpilot::service
{
    // 0014 incremental job fields (docs/03 section 9). Both write paths --
    // POST /internal/jobs/import (manual) and POST /internal/jobs/source-sync
    // (observation) -- must accept and validate the exact same domain, so the
    // rules live here instead of being duplicated per path.
    inline constexpr std::uint32_t kEducationRequiredMin = 1;
    inline constexpr std::uint32_t kEducationRequiredMax = 5;
    inline constexpr std::uint32_t kJobNatureMin = 1;
    inline constexpr std::uint32_t kJobNatureMax = 3;
    inline constexpr std::uint32_t kExperienceYearsMin = 0;
    inline constexpr std::uint32_t kExperienceYearsMax = 32767;

    // Authoritative deadline timezone (docs/03 section 9.1). Recruitment
    // deadlines are Beijing time (Asia/Shanghai); the identifier is recorded
    // here, the offset is a fixed +08:00 because China has had no DST since
    // 1991, and no conversion may use the server's local timezone.
    inline constexpr const char *kDeadlineTimeZoneId = "Asia/Shanghai";
    inline constexpr std::int64_t kBeijingUtcOffsetSeconds = 8 * 60 * 60;

    // A date-only deadline is stored as Beijing local NOON converted to UTC
    // (04:00:00 UTC). The convention exists only so a calendar date round-trips
    // without shifting; it is not an expiry moment and is never rendered as
    // one (docs/03 section 9.1).
    inline constexpr int kDeadlineDateOnlyBeijingHour = 12;
    inline constexpr const char *kDeadlineDateOnlyUtcTime = "04:00:00";

    // Range/enum/time validation for the 0014 fields. Returns false for any
    // value that must map to ErrorCode::kInvalidParam (400); never rewrites,
    // clamps or silently drops a value.
    bool ValidateJobExtraFields(const JobImportData &job);

    // Fills the parser-level default: deadline_date_only is meaningful only
    // together with deadline, and an omitted flag next to a present deadline
    // means "explicit time" (false). A flag without a deadline is left as is so
    // validation can reject it.
    void NormalizeJobExtraFields(JobImportData *job);

    // "YYYY-MM-DD HH:MM:SS" <-> fixed +08:00 Beijing conversion. The input must
    // already be a valid UTC db timestamp; std::nullopt means invalid input or
    // a result outside the representable 1000..9999 range.
    std::optional<std::string> AddSecondsToDbTimestamp(
        const std::string &value,
        std::int64_t seconds);

    // True when the UTC timestamp carries the date-only storage convention
    // (time part 04:00:00).
    bool IsDateOnlyStoredDeadline(const std::string &utcValue);

    // "YYYY-MM-DD" (Beijing calendar date) -> the stored UTC convention value.
    std::optional<std::string> MakeDateOnlyStoredDeadline(
        const std::string &beijingDate);

    // Stored UTC -> Beijing local "YYYY-MM-DD HH:MM:SS".
    std::optional<std::string> DeadlineToBeijingLocal(
        const std::string &utcValue);

    // Stored UTC -> Beijing local calendar date "YYYY-MM-DD".
    std::optional<std::string> DeadlineToBeijingDate(
        const std::string &utcValue);

} // namespace jobpilot::service
