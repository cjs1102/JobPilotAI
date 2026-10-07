#include "jobpilot/service/job/source_sync_dao.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "jobpilot/database/mysql_pool.h"

namespace jobpilot::service
{
    namespace
    {
        jobpilot::core::SqlParam OptionalStringParam(
            const std::optional<std::string> &value)
        {
            if (!value.has_value())
            {
                return nullptr;
            }

            return value.value();
        }

        jobpilot::core::SqlParam OptionalUnsignedParam(
            const std::optional<std::uint32_t> &value)
        {
            if (!value.has_value())
            {
                return nullptr;
            }

            return static_cast<std::uint64_t>(
                value.value());
        }

        JobUpsertResult UpsertObservedJob(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            const SourceObservation &observation,
            const SourceObservationJob &item)
        {
            const JobImportData &job = item.job;

            // published_at and external_id are source-provided facts: a later
            // fetch that omits them must not erase what was already observed.
            const jobpilot::core::ExecutionResult result =
                transaction.Execute(
                    "INSERT INTO jobs "
                    "(company, title, location, salary_min, salary_max, "
                    "salary_currency, description, source, source_url, status, "
                    "source_scope, external_id, published_at, "
                    "last_seen_at, last_verified_at, closed_at, "
                    "missing_count, source_status) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 1, ?, ?, ?, ?, ?, "
                    "NULL, 0, 'ok') "
                    "ON DUPLICATE KEY UPDATE "
                    "id = LAST_INSERT_ID(id), "
                    "company = VALUES(company), "
                    "title = VALUES(title), "
                    "location = VALUES(location), "
                    "salary_min = VALUES(salary_min), "
                    "salary_max = VALUES(salary_max), "
                    "salary_currency = VALUES(salary_currency), "
                    "description = VALUES(description), "
                    "source = VALUES(source), "
                    "source_url = VALUES(source_url), "
                    "source_scope = VALUES(source_scope), "
                    "external_id = COALESCE(VALUES(external_id), external_id), "
                    "published_at = COALESCE(VALUES(published_at), published_at), "
                    "last_seen_at = VALUES(last_seen_at), "
                    "last_verified_at = VALUES(last_verified_at), "
                    "closed_at = NULL, "
                    "missing_count = 0, "
                    "source_status = 'ok', "
                    "status = 1",
                    {job.company,
                     job.title,
                     OptionalStringParam(job.location),
                     OptionalUnsignedParam(job.salaryMin),
                     OptionalUnsignedParam(job.salaryMax),
                     job.salaryCurrency,
                     OptionalStringParam(job.description),
                     job.source,
                     job.sourceUrl,
                     observation.sourceScope,
                     OptionalStringParam(item.externalId),
                     OptionalStringParam(item.publishedAt),
                     observation.observedAt,
                     observation.observedAt});

            if (result.lastInsertId == 0)
            {
                throw std::runtime_error(
                    "source sync upsert returned no id");
            }

            if (result.affectedRows == 1)
            {
                return JobUpsertResult{
                    JobUpsertStatus::kCreated,
                    result.lastInsertId};
            }

            if (result.affectedRows == 0 ||
                result.affectedRows == 2)
            {
                return JobUpsertResult{
                    JobUpsertStatus::kUpdated,
                    result.lastInsertId};
            }

            throw std::runtime_error(
                "source sync upsert returned unexpected affected rows");
        }
    } // namespace

    SourceObservationApplyResult SourceSyncDao::ApplyObservation(
        jobpilot::core::ConnectionPool::Transaction &transaction,
        const SourceObservation &observation)
    {
        SourceObservationApplyResult result;
        result.seen = observation.jobs.size();

        for (const SourceObservationJob &item : observation.jobs)
        {
            const JobUpsertResult upserted =
                UpsertObservedJob(
                    transaction,
                    observation,
                    item);

            if (upserted.status == JobUpsertStatus::kCreated)
            {
                ++result.created;
            }
            else
            {
                ++result.updated;
            }

            requirementsWriter_.ReplaceRequirements(
                transaction,
                upserted.jobId,
                item.requirements);
        }

        return result;
    }

    SourceObservationCloseResult SourceSyncDao::CloseUnseenForScope(
        jobpilot::core::ConnectionPool::Transaction &transaction,
        const SourceObservation &observation,
        const MissingPolicy &policy)
    {
        SourceObservationCloseResult result;

        // An incomplete observation must never change the lifecycle.
        if (!observation.complete)
        {
            return result;
        }

        std::string observedFilter;
        std::vector<jobpilot::core::SqlParam> observedParams;

        if (!observation.jobs.empty())
        {
            observedFilter = " AND source_url NOT IN (";

            for (std::size_t index = 0;
                 index < observation.jobs.size();
                 ++index)
            {
                if (index > 0)
                {
                    observedFilter += ", ";
                }

                observedFilter += "?";

                observedParams.push_back(
                    observation.jobs[index].job.sourceUrl);
            }

            observedFilter += ")";
        }

        std::vector<jobpilot::core::SqlParam> markedParams;
        markedParams.reserve(observedParams.size() + 2);
        markedParams.push_back(observation.source);
        markedParams.push_back(observation.sourceScope);
        markedParams.insert(
            markedParams.end(),
            observedParams.begin(),
            observedParams.end());

        const jobpilot::core::ExecutionResult marked =
            transaction.Execute(
                "UPDATE jobs "
                "SET missing_count = missing_count + 1, "
                "source_status = 'missing' "
                "WHERE source = ? AND source_scope = ? "
                "AND status = 1" +
                    observedFilter,
                markedParams);

        result.missing = marked.affectedRows;

        const jobpilot::core::ExecutionResult closed =
            transaction.Execute(
                "UPDATE jobs "
                "SET status = 0, source_status = 'closed', closed_at = ? "
                "WHERE source = ? AND source_scope = ? "
                "AND status = 1 AND source_status = 'missing' "
                "AND (missing_count >= ? "
                "OR (last_seen_at IS NOT NULL "
                "AND TIMESTAMPDIFF(SECOND, last_seen_at, ?) > ?))",
                {observation.observedAt,
                 observation.source,
                 observation.sourceScope,
                 static_cast<std::int64_t>(
                     policy.closeAfterMisses),
                 observation.observedAt,
                 policy.graceSeconds});

        result.closed = closed.affectedRows;

        return result;
    }

} // namespace jobpilot::service
