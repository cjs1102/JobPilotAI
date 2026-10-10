#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "jobpilot/database/mysql_pool.h"
#include "jobpilot/service/job/job_quality_types.h"

namespace jobpilot::service
{
    struct JobImportData
    {
        std::string company;
        std::string title;
        std::optional<std::string> location;
        std::optional<std::uint32_t> salaryMin;
        std::optional<std::uint32_t> salaryMax;
        std::string salaryCurrency{"CNY"};
        std::optional<std::string> description;
        std::string source;
        std::string sourceUrl;

        // 0014 incremental fields (docs/03 section 9). deadline is a UTC
        // "YYYY-MM-DD HH:MM:SS" instant; deadlineDateOnly marks the date-only
        // storage convention instead of writing an expiry moment.
        std::optional<std::uint32_t> educationRequired;
        std::optional<std::uint32_t> experienceYearsMin;
        std::optional<std::uint32_t> experienceYearsMax;
        std::optional<std::uint32_t> jobNature;
        std::optional<std::string> deadline;
        std::optional<bool> deadlineDateOnly;
    };

    struct JobRequirementImportData
    {
        std::string skill;
        std::int32_t level = 1;
    };

    enum class JobUpsertStatus
    {
        kCreated,
        kUpdated,
        // F14 (0014): a source-sync observation hit a source="manual" row and
        // was skipped instead of overwriting it. Only the observation path can
        // return this; the manual import path never does.
        kSkippedManual
    };

    struct JobUpsertResult
    {
        JobUpsertStatus status = JobUpsertStatus::kCreated;
        std::uint64_t jobId = 0;
    };

    class IJobIngestionDao
    {
    public:
        virtual ~IJobIngestionDao() = default;

        virtual JobUpsertResult UpsertJob(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            const JobImportData &job) = 0;

        virtual void ReplaceRequirements(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t jobId,
            const std::vector<JobRequirementImportData> &requirements) = 0;

        // 0015A: persist the pre-ingest verdict on the row it belongs to,
        // inside the caller's upsert transaction (never a second one).
        virtual void SetReviewStatus(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t jobId,
            std::int32_t status) = 0;

        // Replaces the still-open findings of this job with the current
        // findings; resolved rows stay for the audit trail. Rejected items are
        // never passed here (they write nothing).
        virtual void InsertReviewItems(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t jobId,
            const std::vector<JobQualityFinding> &findings) = 0;
    };

} // namespace jobpilot::service
