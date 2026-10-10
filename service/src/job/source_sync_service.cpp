#include "jobpilot/service/job/source_sync_service.h"

#include <cstddef>
#include <exception>
#include <utility>
#include <vector>

#include "jobpilot/core/logger.h"
#include "jobpilot/database/mysql_pool.h"
#include "jobpilot/service/job/job_quality.h"

namespace jobpilot::service
{
    SourceSyncService::SourceSyncService(
        ISourceSyncDao &sourceSyncDao,
        jobpilot::core::ConnectionPool &pool,
        PlatformNameRegistry platforms)
        : sourceSyncDao_(sourceSyncDao),
          pool_(pool),
          platforms_(std::move(platforms))
    {
    }

    ServiceResult<SourceSyncResult> SourceSyncService::Sync(
        const SourceObservation &observation)
    {
        std::vector<JobImportData> jobData;
        jobData.reserve(observation.jobs.size());

        for (const SourceObservationJob &item : observation.jobs)
        {
            jobData.push_back(item.job);
        }

        // The same deterministic engine as the manual import path: one source
        // of verdicts, no duplicated SQL heuristics (ExecPlan 0015A 5.4.2).
        // It runs before the structural observation validation so a reject
        // carries its {code,field,reason} diagnostic, exactly like the manual
        // import path.
        std::vector<JobQualityReport> reports;
        reports.reserve(jobData.size());

        for (const JobImportData &job : jobData)
        {
            reports.push_back(
                EvaluateJobQuality(job, platforms_));
        }

        MarkDuplicateContentInBatch(jobData, &reports);

        SourceSyncResult result;
        result.complete = observation.complete;
        result.quality = SummarizeJobQuality(jobData, reports);

        // A rejected item writes nothing at all: the observation fails before
        // the transaction opens (ExecPlan 0015A section 6.1).
        if (result.quality.rejected > 0)
        {
            // Byte bound + delivered size for a size-reject diagnostic.
            if (result.quality.diagnosticCode ==
                    "description_too_large")
            {
                for (std::size_t index = 0;
                     index < jobData.size();
                     ++index)
                {
                    if (reports[index].rejected &&
                        jobData[index].description.has_value())
                    {
                        result.quality.diagnosticActualBytes =
                            jobData[index].description->size();
                        break;
                    }
                }
            }

            return ServiceResult<SourceSyncResult>{
                ErrorCode::kInvalidParam,
                std::move(result)};
        }

        if (!ValidateSourceObservation(observation))
        {
            return ServiceResult<SourceSyncResult>{
                ErrorCode::kInvalidParam,
                {}};
        }

        // Stamp the derived verdicts for the transactional apply. The close
        // pass keeps the original observation so a rejected item still counts
        // as "seen" and never closes an existing row.
        SourceObservation stamped = observation;

        for (std::size_t index = 0;
             index < stamped.jobs.size() &&
             index < reports.size();
             ++index)
        {
            stamped.jobs[index].reviewStatus =
                reports[index].reviewStatus;
            stamped.jobs[index].findings =
                reports[index].findings;
        }

        try
        {
            auto transaction = pool_.BeginTransaction();

            const SourceObservationApplyResult applied =
                sourceSyncDao_.ApplyObservation(
                    transaction,
                    stamped);

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
                std::move(result)};
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
