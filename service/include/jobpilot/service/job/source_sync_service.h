#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "jobpilot/service/job/source_sync_dao_interface.h"
#include "jobpilot/service/result.h"

namespace jobpilot::service
{
    // Response counters of POST /internal/jobs/source-sync
    // (ExecPlan 0012 section 7; F14 counters from 0014).
    struct SourceSyncResult
    {
        std::size_t created = 0;
        std::size_t updated = 0;
        std::size_t seen = 0;
        std::size_t closed = 0;
        bool complete = false;
        std::size_t skippedManual = 0;
        std::vector<std::string> skippedManualUrls;
    };

    class SourceSyncService
    {
    public:
        SourceSyncService(
            ISourceSyncDao &sourceSyncDao,
            jobpilot::core::ConnectionPool &pool);

        // One transaction per observation: validate, upsert every observed
        // job plus its requirements, and close missing jobs only for a
        // complete observation (ExecPlan 0012 section 12).
        ServiceResult<SourceSyncResult> Sync(
            const SourceObservation &observation);

    private:
        ISourceSyncDao &sourceSyncDao_;
        jobpilot::core::ConnectionPool &pool_;
    };

} // namespace jobpilot::service
