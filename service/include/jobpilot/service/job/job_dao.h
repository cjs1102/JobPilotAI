#pragma once

#include "jobpilot/service/job/job_dao_interface.h"

namespace jobpilot::core
{
    class ConnectionPool;
}

namespace jobpilot::service
{
    class JobDao final : public IJobDao
    {
    public:
        explicit JobDao(
            jobpilot::core::ConnectionPool &pool);

        std::vector<JobSummary> SearchJobs(
            const std::optional<std::string> &keyword,
            const std::optional<std::string> &location,
            std::size_t limit,
            std::size_t offset) override;

        std::size_t CountJobs(
            const std::optional<std::string> &keyword,
            const std::optional<std::string> &location) override;

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
