#pragma once

#include "jobpilot/service/job/job_dao_interface.h"
#include "jobpilot/service/job/job_ranking_dao_interface.h"

namespace jobpilot::core
{
    class ConnectionPool;
}

namespace jobpilot::service
{
    class JobDao final
        : public IJobDao,
          public IJobRankingDao
    {
    public:
        explicit JobDao(
            jobpilot::core::ConnectionPool &pool);

        std::vector<JobSummary> SearchJobs(
            const JobSearchFilter &filter,
            std::size_t limit,
            std::size_t offset) override;

        std::size_t CountJobs(
            const JobSearchFilter &filter) override;

        std::vector<JobSummary> SearchRankingCandidates(
            const JobSearchFilter &filter,
            std::size_t limit) override;

        // 0015 B3: the strict candidate window (list predicate + list ordering).
        std::vector<JobSummary> SearchListWindow(
            const JobSearchFilter &filter,
            std::size_t limit) override;

        std::size_t CountRankingCandidates(
            const JobSearchFilter &filter) override;

        std::vector<JobRequirementsRecord> ListRequirementsForJobs(
            const std::vector<std::uint64_t> &jobIds) override;

        std::optional<JobDetail> FindActiveJobById(
            std::uint64_t jobId) override;

        bool ActiveJobExists(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t jobId) override;

        std::vector<JobRequirement> ListRequirements(
            std::uint64_t jobId) override;

    private:
        jobpilot::core::ConnectionPool &pool_;
    };

} // namespace jobpilot::service
