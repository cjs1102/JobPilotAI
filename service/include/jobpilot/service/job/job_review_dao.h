#pragma once

#include "jobpilot/service/job/job_review_dao_interface.h"

namespace jobpilot::service
{
    class JobReviewDao final : public IJobReviewDao
    {
    public:
        explicit JobReviewDao(
            jobpilot::core::ConnectionPool &pool);

        std::size_t CountQueue(std::int32_t status) override;

        std::vector<JobReviewQueueItem> ListQueue(
            std::int32_t status,
            std::size_t limit,
            std::size_t offset) override;

        std::optional<JobReviewState> FindState(
            std::uint64_t jobId) override;

        void ApplyDecision(
            std::uint64_t jobId,
            JobReviewDecision decision,
            const std::string &resolver,
            const std::string &note,
            const JobReviewFix &fix,
            const std::vector<JobReviewAuditField> &audit) override;

        std::size_t CountActiveJobs() override;

        std::size_t CountByReviewStatus(
            std::int32_t status) override;

        std::vector<JobReviewCandidate> ListActiveCandidates(
            std::size_t limit) override;

        void ApplyBackfill(
            const std::vector<JobReviewBackfillEntry> &flagged,
            const std::string &actor) override;

        std::size_t RevertBackfill() override;

    private:
        jobpilot::core::ConnectionPool &pool_;
    };

} // namespace jobpilot::service
