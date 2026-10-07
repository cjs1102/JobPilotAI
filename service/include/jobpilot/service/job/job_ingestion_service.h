#pragma once

#include <cstddef>
#include <vector>

#include "jobpilot/service/job/job_ingestion_dao_interface.h"
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
    };

    class JobIngestionService
    {
    public:
        JobIngestionService(
            IJobIngestionDao &jobIngestionDao,
            jobpilot::core::ConnectionPool &pool);

        ServiceResult<JobIngestionResult> Import(
            const std::vector<JobIngestionItem> &jobs);

    private:
        static bool Validate(
            const std::vector<JobIngestionItem> &jobs);

        IJobIngestionDao &jobIngestionDao_;
        jobpilot::core::ConnectionPool &pool_;
    };

} // namespace jobpilot::service
