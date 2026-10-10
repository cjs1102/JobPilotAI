#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "jobpilot/service/job/job_quality_types.h"

namespace jobpilot::service
{
    // GET /internal/jobs/review-queue item (ExecPlan 0015A section 5.2).
    // Each queued job reports the review reason (stable code + human text), its
    // current review status and the job identifiers needed to act on it.
    struct JobReviewQueueItem
    {
        std::uint64_t jobId = 0;
        std::string title;
        std::string company;
        std::optional<std::string> location;
        std::optional<std::string> sourceUrl;
        std::string source;
        std::optional<std::string> descriptionExcerpt;
        std::int32_t reviewStatus = 1;
        std::string reasonCode;
        std::string reasonText;
        std::string outcome;
        std::string resolution;
        std::string createTime;
    };

    struct JobReviewQueuePage
    {
        std::vector<JobReviewQueueItem> items;
        std::size_t total = 0;
        std::size_t page = 1;
        std::size_t pageSize = 20;
    };

    enum class JobReviewDecision
    {
        kApprove = 0,
        kFix = 1,
        kReject = 2
    };

    const char *ToString(JobReviewDecision decision);
    bool ParseJobReviewDecision(
        const std::string &value,
        JobReviewDecision *decision);

    // `fix` fields are optional; only the provided ones are re-validated
    // against the 0015A section 5.5 contracts and applied.
    struct JobReviewFix
    {
        std::optional<std::string> company;
        std::optional<std::string> title;
        std::optional<std::string> sourceUrl;
        std::optional<std::string> description;
    };

    struct JobReviewResult
    {
        std::uint64_t jobId = 0;
        std::int32_t reviewStatus = 0;
        std::string decision;
        std::string reviewer;
        std::string note;
        // True when the identical decision was already recorded: an idempotent
        // 200 no-op that changed nothing.
        bool idempotent = false;
        std::vector<std::string> changedFields;

        // Set only when a `fix` payload fails §5.5 re-validation; the state is
        // unchanged and the controller reports {code,field,reason}.
        std::string diagnosticCode;
        std::string diagnosticField;
        std::string diagnosticReason;
    };

    struct JobReviewAuditField
    {
        std::string field;
        std::string before;
        std::string after;
    };

    // Current review state of one job (used for the approve/fix/reject
    // state machine).
    struct JobReviewState
    {
        std::uint64_t jobId = 0;
        std::int32_t reviewStatus = 0;
        std::string reviewNote;
        std::string reviewedAt;
        // Newest audit row decision: '' when the row has no review history yet.
        std::string lastResolution;
        // Current stored values, used for the per-field before/after audit.
        std::string company;
        std::string title;
        std::string sourceUrl;
        std::string description;
    };

    // One active row as read for the historical classification (measure ->
    // classify -> dry-run). The fields mirror the ingest DTO so the SAME
    // deterministic check function classifies historical rows.
    struct JobReviewCandidate
    {
        std::uint64_t jobId = 0;
        std::int32_t reviewStatus = 0;
        std::string company;
        std::string title;
        std::optional<std::string> location;
        std::optional<std::uint32_t> salaryMin;
        std::optional<std::uint32_t> salaryMax;
        std::string salaryCurrency;
        std::optional<std::string> description;
        std::string source;
        std::string sourceUrl;
    };

    struct JobReviewBackfillReport
    {
        std::string mode;
        std::size_t activeJobs = 0;
        std::size_t scanned = 0;
        std::size_t wouldFlag = 0;
        std::size_t hiddenSharePercent = 0;
        std::size_t thresholdPercent = 5;
        std::size_t thresholdRows = 100;
        bool thresholdExceeded = false;
        std::map<std::string, std::size_t> perReason;
        std::vector<JobReviewQueueItem> sample;
        std::size_t applied = 0;
        std::size_t reverted = 0;
        std::size_t beforeNeedsReview = 0;
        std::size_t afterNeedsReview = 0;
    };

    // One classified historical row: the verdict the SAME engine would produce
    // for its stored fields.
    struct JobReviewBackfillEntry
    {
        std::uint64_t jobId = 0;
        std::vector<JobQualityFinding> findings;
    };

} // namespace jobpilot::service
