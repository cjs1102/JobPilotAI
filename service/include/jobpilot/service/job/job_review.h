#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "jobpilot/service/job/job_quality.h"
#include "jobpilot/service/job/job_review_dao_interface.h"
#include "jobpilot/service/job/job_review_types.h"
#include "jobpilot/service/result.h"

namespace jobpilot::service
{
    // 0015A review queue, approve/fix/reject and the auditable historical
    // procedure (ExecPlan 0015A sections 5.2, 5.4.2 and 6.2).
    class JobReviewService
    {
    public:
        JobReviewService(
            IJobReviewDao &dao,
            PlatformNameRegistry platforms =
                PlatformNameRegistry::LoadFromEnvironment(nullptr));

        // status: needs_review (default) | quarantined | clean.
        ServiceResult<JobReviewQueuePage> ListQueue(
            const std::string &status,
            std::size_t page,
            std::size_t pageSize);

        // decision: approve | fix | reject.
        ServiceResult<JobReviewResult> Review(
            std::uint64_t jobId,
            const std::string &decision,
            const std::string &reviewer,
            const std::string &note,
            const JobReviewFix &fix);

        // mode: dry_run | apply | revert.
        ServiceResult<JobReviewBackfillReport> Backfill(
            const std::string &mode,
            const std::string &actor);

    private:
        IJobReviewDao &dao_;
        PlatformNameRegistry platforms_;
    };

} // namespace jobpilot::service
