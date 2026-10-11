#include "jobpilot/service/job/job_service.h"

#include <exception>
#include <limits>
#include <utility>

#include "jobpilot/core/logger.h"

namespace jobpilot::service
{
    namespace
    {
        constexpr std::size_t kMaxPageSize = 100;
    } // namespace

    JobService::JobService(
        IJobDao &jobDao)
        : jobDao_(jobDao)
    {
    }

    ServiceResult<JobPage> JobService::SearchJobs(
        const JobSearchFilter &filter,
        std::size_t page,
        std::size_t pageSize)
    {
        if (page == 0 ||
            pageSize == 0 ||
            pageSize > kMaxPageSize)
        {
            return ServiceResult<JobPage>{
                ErrorCode::kInvalidParam,
                {}};
        }

        JobSearchFilter normalized;

        if (!NormalizeJobSearchFilter(
                filter,
                &normalized))
        {
            return ServiceResult<JobPage>{
                ErrorCode::kInvalidParam,
                {}};
        }

        const std::size_t pageIndex = page - 1;

        if (pageIndex >
            std::numeric_limits<std::size_t>::max() /
                pageSize)
        {
            return ServiceResult<JobPage>{
                ErrorCode::kInvalidParam,
                {}};
        }

        const std::size_t offset =
            pageIndex * pageSize;

        try
        {
            JobPage result;
            result.page = page;
            result.pageSize = pageSize;
            result.total =
                jobDao_.CountJobs(normalized);
            result.list =
                jobDao_.SearchJobs(
                    normalized,
                    pageSize,
                    offset);

            return ServiceResult<JobPage>{
                ErrorCode::kSuccess,
                std::move(result)};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "job search failed: {}",
                error.what());

            return ServiceResult<JobPage>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

    ServiceResult<JobDetail> JobService::GetJob(
        std::uint64_t jobId,
        bool includeNeedsReview)
    {
        if (jobId == 0)
        {
            return ServiceResult<JobDetail>{
                ErrorCode::kInvalidParam,
                {}};
        }

        try
        {
            // Public semantics: a job still queued for review is reported as
            // not found, so the detail endpoint cannot confirm that it exists.
            std::optional<JobDetail> detail =
                jobDao_.FindPublicJobById(
                    jobId,
                    includeNeedsReview);

            if (!detail.has_value())
            {
                return ServiceResult<JobDetail>{
                    ErrorCode::kJobNotFound,
                    {}};
            }

            detail->requirements =
                jobDao_.ListRequirements(jobId);

            return ServiceResult<JobDetail>{
                ErrorCode::kSuccess,
                std::move(detail.value())};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "job detail lookup failed: {}",
                error.what());

            return ServiceResult<JobDetail>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

} // namespace jobpilot::service
