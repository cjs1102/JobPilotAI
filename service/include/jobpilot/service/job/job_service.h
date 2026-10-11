#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "jobpilot/service/job/job_dao_interface.h"
#include "jobpilot/service/job/job_search_filter.h"
#include "jobpilot/service/result.h"

namespace jobpilot::service
{
    struct JobPage
    {
        std::vector<JobSummary> list;
        std::size_t total = 0;
        std::size_t page = 1;
        std::size_t pageSize = 20;
    };

    class JobService
    {
    public:
        explicit JobService(IJobDao &jobDao);

        ServiceResult<JobPage> SearchJobs(
            const JobSearchFilter &filter,
            std::size_t page,
            std::size_t pageSize);

        // Public detail read. `includeNeedsReview` is the 0015A rollback switch
        // value (see job_visibility.h); it is threaded in rather than read here
        // so every public read in one request uses the same decision.
        ServiceResult<JobDetail> GetJob(
            std::uint64_t jobId,
            bool includeNeedsReview);

    private:
        IJobDao &jobDao_;
    };

} // namespace jobpilot::service
