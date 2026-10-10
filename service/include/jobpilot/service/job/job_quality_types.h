#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace jobpilot::service
{
    // 0015A three deterministic pre-ingest outcomes
    // (ExecPlan 0015A section 6.1).
    //  reject      -> cannot be safely normalised; nothing is written
    //  needs_review-> stored, hidden from public search, queued with a reason
    //  advisory    -> stored and visible; recorded for audit only
    //
    // Declared in a leaf header so the ingestion DAO interface can consume
    // findings without depending on the whole quality engine.
    enum class JobQualityOutcome
    {
        kReject = 0,
        kNeedsReview = 1,
        kAdvisory = 2
    };

    // Database `job_review_items.outcome` vocabulary.
    const char *ToString(JobQualityOutcome outcome);

    struct JobQualityFinding
    {
        std::string code;    // stable machine code
        std::string reason;  // human readable
        JobQualityOutcome outcome = JobQualityOutcome::kAdvisory;
    };

    // One request/observation item in an ingest response (ExecPlan 0015A 5.2).
    // A rejected item writes nothing; the diagnostic carries the stable code,
    // the affected field and the human-readable reason.
    struct JobQualityItemResult
    {
        std::size_t index = 0;
        std::string sourceUrl;
        std::int32_t reviewStatus = 0;
        bool rejected = false;
        std::string diagnosticCode;
        std::string diagnosticField;
        std::string diagnosticReason;
        std::vector<JobQualityFinding> findings;
    };

    // Shared ingest-response projection (ExecPlan 0015A 5.2): per-item findings
    // plus the needs_review/rejected/clean counters.
    struct JobQualitySummary
    {
        std::size_t needsReview = 0;
        std::size_t rejected = 0;
        std::size_t clean = 0;
        std::string diagnosticCode;
        std::string diagnosticField;
        std::string diagnosticReason;
        // Byte bound of a size-reject (0 when the code has no byte limit), so
        // the diagnostic can carry limit_bytes/actual_bytes (docs/04 5.4).
        std::size_t diagnosticLimitBytes = 0;
        std::size_t diagnosticActualBytes = 0;
        std::vector<JobQualityItemResult> items;
    };

    // Stable, field-level name reported with a reject diagnostic. Returns an
    // empty string for an unknown code.
    const char *JobQualityRejectField(const std::string &code);

    // Byte bound of a size-reject code (0 when the code is not size-based), so
    // the ingest diagnostic can report limit_bytes/actual_bytes.
    std::size_t JobQualityRejectLimitBytes(
        const std::string &code);

} // namespace jobpilot::service
