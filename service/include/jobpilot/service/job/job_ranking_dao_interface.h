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

        // 0015 B3: candidate window that shares the PUBLIC LIST's ordering
        // (including `filter.sort`), so a strict-filtered set can be paginated
        // consistently with the list page.
        virtual std::vector<JobSummary> SearchListWindow(
            const JobSearchFilter &filter,
            std::size_t limit) = 0;

        virtual std::size_t CountRankingCandidates(
            const JobSearchFilter &filter) = 0;

        virtual std::vector<JobRequirementsRecord>
        ListRequirementsForJobs(
            const std::vector<std::uint64_t> &jobIds) = 0;
    };

} // namespace jobpilot::service
