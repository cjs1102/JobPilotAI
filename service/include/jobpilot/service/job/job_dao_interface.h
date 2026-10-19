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
        // 0020: the explicit application link from the source, if any. The
        // public API always emits the key (null when absent).
        std::optional<std::string> applyUrl;
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

        // PUBLIC detail semantics: an active job that is publicly visible
        // (review_status = 0, unless the 0015A rollback switch is on). Returns
        // nullopt for a queued or quarantined row, exactly as for a row that
        // does not exist, so a caller cannot learn that a hidden job exists.
        virtual std::optional<JobDetail> FindPublicJobById(
            std::uint64_t jobId,
            bool includeNeedsReview) = 0;

        // INTERNAL semantics for ops and worker paths only: an active job
        // regardless of its review state. The review queue, the review decision
        // and the AI worker's task input use this; nothing that serves an
        // unauthenticated or ordinary user request may.
        virtual std::optional<JobDetail> FindReviewableJobById(
            std::uint64_t jobId) = 0;

        // PUBLIC existence check used when an application is created, with the
        // same visibility rule as FindPublicJobById.
        virtual bool PublicJobExists(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t jobId,
            bool includeNeedsReview) = 0;

        virtual std::vector<JobRequirement> ListRequirements(
            std::uint64_t jobId) = 0;
    };

} // namespace jobpilot::service
