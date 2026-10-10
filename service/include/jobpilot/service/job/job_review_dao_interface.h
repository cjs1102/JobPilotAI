#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "jobpilot/database/mysql_pool.h"
#include "jobpilot/service/job/job_review_types.h"

namespace jobpilot::service
{
    // 0015A review/backfill persistence. Reads own the pool; every write runs
    // inside one transaction opened by the DAO itself.
    class IJobReviewDao
    {
    public:
        virtual ~IJobReviewDao() = default;

        virtual std::size_t CountQueue(std::int32_t status) = 0;

        virtual std::vector<JobReviewQueueItem> ListQueue(
            std::int32_t status,
            std::size_t limit,
            std::size_t offset) = 0;

        // nullopt when the job does not exist.
        virtual std::optional<JobReviewState> FindState(
            std::uint64_t jobId) = 0;

        // One transaction: the job row plus its audit rows.
        //  resolution   'approved' | 'fixed' | 'rejected'
        //  fix          only the provided fields are written (COALESCE keeps
        //               the stored value otherwise)
        //  audit        per-field before/after rows (may be empty)
        virtual void ApplyDecision(
            std::uint64_t jobId,
            JobReviewDecision decision,
            const std::string &resolver,
            const std::string &note,
            const JobReviewFix &fix,
            const std::vector<JobReviewAuditField> &audit) = 0;

        virtual std::size_t CountActiveJobs() = 0;

        virtual std::size_t CountByReviewStatus(
            std::int32_t status) = 0;

        virtual std::vector<JobReviewCandidate> ListActiveCandidates(
            std::size_t limit) = 0;

        // Marks the classified rows needs_review and records the findings in
        // one transaction per call. Idempotent: an existing open backfill
        // verdict for the job is refreshed, never duplicated.
        virtual void ApplyBackfill(
            const std::vector<JobReviewBackfillEntry> &flagged,
            const std::string &actor) = 0;

        // Restores review_status = 0 for rows whose only open audit rows came
        // from a backfill, and removes those rows. Returns the job count.
        virtual std::size_t RevertBackfill() = 0;
    };

} // namespace jobpilot::service
