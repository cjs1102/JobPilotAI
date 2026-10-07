#pragma once

#include "jobpilot/service/job/job_ingestion_dao_interface.h"

namespace jobpilot::service
{
    class JobIngestionDao final : public IJobIngestionDao
    {
    public:
        JobIngestionDao() = default;

        JobUpsertResult UpsertJob(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            const JobImportData &job) override;

        void ReplaceRequirements(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t jobId,
            const std::vector<JobRequirementImportData> &requirements) override;
    };

} // namespace jobpilot::service
