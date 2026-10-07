#include "jobpilot/service/job/job_service.h"

#include <cctype>
#include <exception>
#include <limits>
#include <optional>
#include <utility>

#include "jobpilot/core/logger.h"

namespace jobpilot::service
{
    namespace
    {
        constexpr std::size_t kMaxKeywordBytes = 128;
        constexpr std::size_t kMaxLocationBytes = 64;
        constexpr std::size_t kMaxPageSize = 100;

        std::optional<std::string> NormalizeOptional(
            const std::string &value)
        {
            if (value.empty())
            {
                return std::nullopt;
            }

            return value;
        }
    } // namespace

    JobService::JobService(
        IJobDao &jobDao)
        : jobDao_(jobDao)
    {
    }

    std::string JobService::Trim(
        const std::string &value)
    {
        std::size_t begin = 0;
        std::size_t end = value.size();

        while (begin < end &&
               std::isspace(
                   static_cast<unsigned char>(
                       value[begin])))
        {
            ++begin;
        }

        while (end > begin &&
               std::isspace(
                   static_cast<unsigned char>(
                       value[end - 1])))
        {
            --end;
        }

        return value.substr(
            begin,
            end - begin);
    }

    ServiceResult<JobPage> JobService::SearchJobs(
        const std::string &keyword,
        const std::string &location,
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

        const std::string normalizedKeyword =
            Trim(keyword);
        const std::string normalizedLocation =
            Trim(location);

        if (normalizedKeyword.size() > kMaxKeywordBytes ||
            normalizedLocation.size() > kMaxLocationBytes)
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

        const std::optional<std::string> keywordFilter =
            NormalizeOptional(normalizedKeyword);
        const std::optional<std::string> locationFilter =
            NormalizeOptional(normalizedLocation);

        try
        {
            JobPage result;
            result.page = page;
            result.pageSize = pageSize;
            result.total =
                jobDao_.CountJobs(
                    keywordFilter,
                    locationFilter);
            result.list =
                jobDao_.SearchJobs(
                    keywordFilter,
                    locationFilter,
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
        std::uint64_t jobId)
    {
        if (jobId == 0)
        {
            return ServiceResult<JobDetail>{
                ErrorCode::kInvalidParam,
                {}};
        }

        try
        {
            std::optional<JobDetail> detail =
                jobDao_.FindActiveJobById(jobId);

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
