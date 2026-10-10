#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "jobpilot/database/mysql_pool.h"
#include "jobpilot/service/job/job_search_filter.h"
#include "jobpilot/service/job/source_observation.h"

namespace jobpilot::service
{
    struct JobSummary
    {
        std::uint64_t id = 0;
        std::string company;
        std::string title;
        std::optional<std::string> location;
        std::optional<std::uint32_t> salaryMin;
        std::optional<std::uint32_t> salaryMax;
        std::string salaryCurrency{"CNY"};
        std::string source;
        std::string createTime;
        std::string updateTime;

        // 0012 freshness projection. first_seen_at is createTime (ADR-0010).
        // freshness is derived, never stored.
        std::optional<std::string> publishedAt;
        std::optional<std::string> lastSeenAt;
        std::optional<std::string> lastVerifiedAt;
        std::optional<std::string> sourceStatus;
        // 0015A: the original posting URL is part of the LIST projection too, so
        // a caller need not fetch every detail row to open the posting. The
        // detail path copies its own value here so both agree byte for byte.
        std::optional<std::string> sourceUrl;
        std::optional<FreshnessState> freshness;

        // 0014 incremental projection (docs/03 section 9). deadline is the
        // stored UTC instant; deadlineDateOnly is std::nullopt when the kind is
        // unknown, so the response never fabricates precision.
        std::optional<std::uint32_t> educationRequired;
        std::optional<std::uint32_t> experienceYearsMin;
        std::optional<std::uint32_t> experienceYearsMax;
        std::optional<std::uint32_t> jobNature;
        std::optional<std::string> deadline;
        std::optional<bool> deadlineDateOnly;
    };

    struct JobRequirement
    {
        std::string skill;
        std::int32_t level = 1;
    };

    struct JobDetail
    {
        JobSummary summary;
        std::optional<std::string> description;
        std::vector<JobRequirement> requirements;
        std::optional<std::string> sourceUrl;
        std::int32_t status = 1;
    };

    class IJobDao
    {
    public:
        virtual ~IJobDao() = default;

        virtual std::vector<JobSummary> SearchJobs(
            const JobSearchFilter &filter,
            std::size_t limit,
            std::size_t offset) = 0;

        virtual std::size_t CountJobs(
            const JobSearchFilter &filter) = 0;

        virtual std::optional<JobDetail> FindActiveJobById(
            std::uint64_t jobId) = 0;

        virtual bool ActiveJobExists(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t jobId) = 0;

        virtual std::vector<JobRequirement> ListRequirements(
            std::uint64_t jobId) = 0;
    };

} // namespace jobpilot::service
