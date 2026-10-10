#pragma once

#include <cstddef>
#include <vector>

#include "jobpilot/service/job/job_ingestion_dao_interface.h"
#include "jobpilot/service/job/job_quality.h"
#include "jobpilot/service/result.h"

namespace jobpilot::core
{
    class ConnectionPool;
}

namespace jobpilot::service
{
    struct JobIngestionItem
    {
        JobImportData job;
        std::vector<JobRequirementImportData> requirements;
    };

    struct JobIngestionResult
    {
        std::size_t created = 0;
        std::size_t updated = 0;
        std::size_t total = 0;

        // 0015A: per-item findings + needs_review/rejected/clean counters.
        JobQualitySummary quality;
    };

    class JobIngestionService
    {
    public:
        JobIngestionService(
            IJobIngestionDao &jobIngestionDao,
            jobpilot::core::ConnectionPool &pool,
            // 0015A item 2: the composition boundary resolves the single
            // platform-name registry (env override + repo-relative default)
            // unless a caller injects one.
            PlatformNameRegistry platforms =
                PlatformNameRegistry::LoadFromEnvironment(nullptr));

        ServiceResult<JobIngestionResult> Import(
            const std::vector<JobIngestionItem> &jobs);

    private:
        static bool Validate(
            const std::vector<JobIngestionItem> &jobs);

        IJobIngestionDao &jobIngestionDao_;
        jobpilot::core::ConnectionPool &pool_;
        PlatformNameRegistry platforms_;
    };

} // namespace jobpilot::service
