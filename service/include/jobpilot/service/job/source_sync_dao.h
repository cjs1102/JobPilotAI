#pragma once

#include "jobpilot/service/job/job_ingestion_dao.h"
#include "jobpilot/service/job/source_sync_dao_interface.h"

namespace jobpilot::service
{
    class SourceSyncDao final : public ISourceSyncDao
    {
    public:
        SourceSyncDao() = default;

        SourceObservationApplyResult ApplyObservation(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            const SourceObservation &observation) override;

        SourceObservationCloseResult CloseUnseenForScope(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            const SourceObservation &observation,
            const MissingPolicy &policy) override;

    private:
        // Reuses the ingestion DAO requirement-replacement statement so the
        // requirements contract stays single-sourced.
        JobIngestionDao requirementsWriter_;
    };

} // namespace jobpilot::service
