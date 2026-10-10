#include "jobpilot/service/job/source_sync_service.h"

#include <exception>

#include "jobpilot/core/logger.h"
#include "jobpilot/database/mysql_pool.h"

namespace jobpilot::service
{
    SourceSyncService::SourceSyncService(
        ISourceSyncDao &sourceSyncDao,
        jobpilot::core::ConnectionPool &pool)
        : sourceSyncDao_(sourceSyncDao),
          pool_(pool)
    {
    }

    ServiceResult<SourceSyncResult> SourceSyncService::Sync(
        const SourceObservation &observation)
    {
        if (!ValidateSourceObservation(observation))
        {
            return ServiceResult<SourceSyncResult>{
                ErrorCode::kInvalidParam,
                {}};
        }

        try
        {
            SourceSyncResult result;
            result.complete = observation.complete;

            auto transaction = pool_.BeginTransaction();

            const SourceObservationApplyResult applied =
                sourceSyncDao_.ApplyObservation(
                    transaction,
                    observation);

            result.created = applied.created;
            result.updated = applied.updated;
            result.seen = applied.seen;
            result.skippedManual = applied.skippedManual;
            result.skippedManualUrls =
                applied.skippedManualUrls;

            // F14: the skip must be diagnosable outside the HTTP response too.
            for (const std::string &sourceUrl :
                 applied.skippedManualUrls)
            {
                jobpilot::core::Logger::Get()->warn(
                    "source sync skipped manual job for source_url: {}",
                    sourceUrl);
            }

            if (observation.complete)
            {
                const SourceObservationCloseResult closeResult =
                    sourceSyncDao_.CloseUnseenForScope(
                        transaction,
                        observation,
                        MissingPolicy{});

                result.closed = closeResult.closed;
            }

            transaction.Commit();

            return ServiceResult<SourceSyncResult>{
                ErrorCode::kSuccess,
                result};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "source sync failed: {}",
                error.what());

            return ServiceResult<SourceSyncResult>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

} // namespace jobpilot::service
