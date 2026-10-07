#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "jobpilot/database/mysql_pool.h"

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
    };

    struct JobRequirementImportData
    {
        std::string skill;
        std::int32_t level = 1;
    };

    enum class JobUpsertStatus
    {
        kCreated,
        kUpdated
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
    };

} // namespace jobpilot::service
