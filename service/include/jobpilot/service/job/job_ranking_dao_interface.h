#pragma once

#include <cstdint>
#include <vector>

#include "jobpilot/service/job/job_dao_interface.h"
#include "jobpilot/service/job/job_search_filter.h"

namespace jobpilot::service
{
    struct JobRequirementsRecord
    {
        std::uint64_t jobId = 0;
        std::vector<JobRequirement> requirements;
    };

    class IJobRankingDao
    {
    public:
        virtual ~IJobRankingDao() = default;

        virtual std::vector<JobSummary> SearchRankingCandidates(
            const JobSearchFilter &filter,
            std::size_t limit) = 0;

        virtual std::size_t CountRankingCandidates(
            const JobSearchFilter &filter) = 0;

        virtual std::vector<JobRequirementsRecord>
        ListRequirementsForJobs(
            const std::vector<std::uint64_t> &jobIds) = 0;
    };

} // namespace jobpilot::service
