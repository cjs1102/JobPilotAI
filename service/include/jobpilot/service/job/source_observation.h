#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "jobpilot/service/job/job_ingestion_dao_interface.h"
#include "jobpilot/service/job/job_quality_types.h"

namespace jobpilot::service
{
    // Source observation lifecycle vocabulary (ADR-0010, ExecPlan 0012).
    enum class SourceStatus
    {
        kUnknown,
        kOk,
        kMissing,
        kClosed
    };

    enum class FreshnessState
    {
        kFresh,
        kAging,
        kStale,
        kClosed
    };

    const char *ToString(SourceStatus status);
    bool ParseSourceStatus(
        const std::string &value,
        SourceStatus *status);
    const char *ToString(FreshnessState state);

    // One observation batch limit (ExecPlan 0012 section 7):
    //   complete=true  -> 0..200 (0 is an empty board)
    //   complete=false -> 1..200
    inline constexpr std::size_t kMaxObservationBatchSize = 200;

    inline constexpr std::int32_t kDefaultCloseAfterMisses = 2;
    inline constexpr std::int64_t kDefaultGraceSeconds =
        48 * 60 * 60;
    inline constexpr std::int64_t kFreshMaxAgeSeconds =
        24 * 60 * 60;
    inline constexpr std::int64_t kAgingMaxAgeSeconds =
        7 * 24 * 60 * 60;

    // Per-source closure policy (ADR-0010 section 3).
    struct MissingPolicy
    {
        std::int32_t closeAfterMisses = kDefaultCloseAfterMisses;
        std::int64_t graceSeconds = kDefaultGraceSeconds;
    };

    // One normalized job inside an observation. The shared job fields reuse the
    // existing normalized DTO; externalId/publishedAt are the additive 0012
    // observation fields. source_scope lives on the observation, not the job.
    struct SourceObservationJob
    {
        JobImportData job;
        std::optional<std::string> externalId;
        std::optional<std::string> publishedAt;
        std::vector<JobRequirementImportData> requirements;

        // 0015A derived pre-ingest verdict, stamped by SourceSyncService right
        // before the transactional apply. Not part of the wire payload and not
        // used for rejected items (they never reach the DAO).
        std::int32_t reviewStatus = 0;
        std::vector<JobQualityFinding> findings;
    };

    // One adapter run against a single (source, source_scope) board.
    struct SourceObservation
    {
        std::string source;
        std::string sourceScope;
        bool complete = false;
        std::string observedAt;
        std::vector<SourceObservationJob> jobs;
    };

    bool IsSupportedObservationSource(
        const std::string &source);

    // "YYYY-MM-DD HH:MM:SS" in database-local form. Range checked, including
    // month length and leap years; no timezone conversion is performed here.
    bool IsValidDbTimestamp(
        const std::string &value);

    // Field-level and batch-level validation for
    // POST /internal/jobs/source-sync. A false result maps to
    // ErrorCode::kInvalidParam.
    bool ValidateSourceObservation(
        const SourceObservation &observation);

    // missingCount is the count after incrementing for the current complete
    // absence. Closure happens once the count threshold is reached, or once the
    // grace window since last_seen_at is strictly exceeded (ADR-0010 section 3).
    bool ShouldCloseMissingJob(
        std::int32_t missingCount,
        const std::optional<std::int64_t> &secondsSinceLastSeen,
        const MissingPolicy &policy = MissingPolicy{});

    // closed_at set -> kClosed. No last_seen_at (legacy manual/import rows)
    // -> std::nullopt; the public search policy for that case belongs to the
    // 0012 search step. Negative age (clock skew) is clamped to fresh.
    std::optional<FreshnessState> ClassifyFreshness(
        bool closed,
        const std::optional<std::int64_t> &ageSeconds);

} // namespace jobpilot::service
