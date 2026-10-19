#include "jobpilot/service/job/job_ingestion_dao.h"

#include <cstdint>
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

        // TINYINT/SMALLINT columns (0014) travel as signed integers so the
        // SqlParam variant stays minimal; validation bounds them first.
        jobpilot::core::SqlParam OptionalSmallUnsignedParam(
            const std::optional<std::uint32_t> &value)
        {
            if (!value.has_value())
            {
                return nullptr;
            }

            return static_cast<std::int64_t>(
                value.value());
        }

        jobpilot::core::SqlParam OptionalBoolParam(
            const std::optional<bool> &value)
        {
            if (!value.has_value())
            {
                return nullptr;
            }

            return static_cast<std::int64_t>(
                value.value() ? 1 : 0);
        }

        // The date-kind flag is meaningful only next to a deadline; without a
        // deadline the stored value must stay NULL ("kind unknown").
        jobpilot::core::SqlParam OptionalDeadlineKindParam(
            const JobImportData &job)
        {
            if (!job.deadline.has_value())
            {
                return nullptr;
            }

            return OptionalBoolParam(job.deadlineDateOnly);
        }

        // VARCHAR(255)/VARCHAR(64) count characters under utf8mb4, so cut on a
        // UTF-8 code-point boundary instead of mid-sequence.
        std::string TruncateUtf8(
            const std::string &value,
            std::size_t maxChars)
        {
            std::size_t chars = 0;
            std::size_t index = 0;

            while (index < value.size() && chars < maxChars)
            {
                const unsigned char lead =
                    static_cast<unsigned char>(value[index]);

                std::size_t width = 1;

                if ((lead & 0xE0U) == 0xC0U)
                {
                    width = 2;
                }
                else if ((lead & 0xF0U) == 0xE0U)
                {
                    width = 3;
                }
                else if ((lead & 0xF8U) == 0xF0U)
                {
                    width = 4;
                }

                if (index + width > value.size())
                {
                    break;
                }

                index += width;
                ++chars;
            }

            return value.substr(0, index);
        }
    } // namespace

    JobUpsertResult JobIngestionDao::UpsertJob(
        jobpilot::core::ConnectionPool::Transaction &transaction,
        const JobImportData &job)
    {
        const jobpilot::core::ExecutionResult result =
            transaction.Execute(
                "INSERT INTO jobs "
                "(company, title, location, salary_min, salary_max, "
                "salary_currency, description, source, source_url, apply_url, "
                "status, education_required, experience_years_min, "
                "experience_years_max, job_nature, deadline, "
                "deadline_date_only, published_at) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 1, ?, ?, ?, ?, ?, ?, ?) "
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
                "apply_url = COALESCE(VALUES(apply_url), apply_url), "
                "education_required = VALUES(education_required), "
                "experience_years_min = VALUES(experience_years_min), "
                "experience_years_max = VALUES(experience_years_max), "
                "job_nature = VALUES(job_nature), "
                "deadline = VALUES(deadline), "
                "deadline_date_only = VALUES(deadline_date_only), "
                // 0017A: a payload that omits published_at must not clear an
                // already stored value (same rule as source_sync_dao.cpp:154).
                "published_at = COALESCE(VALUES(published_at), published_at), "
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
                 OptionalStringParam(job.applyUrl),
                 OptionalSmallUnsignedParam(job.educationRequired),
                 OptionalSmallUnsignedParam(job.experienceYearsMin),
                 OptionalSmallUnsignedParam(job.experienceYearsMax),
                 OptionalSmallUnsignedParam(job.jobNature),
                 OptionalStringParam(job.deadline),
                 OptionalDeadlineKindParam(job),
                 OptionalStringParam(job.publishedAt)});

        if (result.lastInsertId == 0)
        {
            throw std::runtime_error(
                "job upsert returned no id");
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
            "job upsert returned unexpected affected rows");
    }

    void JobIngestionDao::ReplaceRequirements(
        jobpilot::core::ConnectionPool::Transaction &transaction,
        std::uint64_t jobId,
        const std::vector<JobRequirementImportData> &requirements)
    {
        if (jobId == 0)
        {
            throw std::invalid_argument(
                "job id must not be zero");
        }

        transaction.Execute(
            "DELETE FROM job_requirements WHERE job_id = ?",
            {jobId});

        for (const JobRequirementImportData &requirement :
             requirements)
        {
            const jobpilot::core::ExecutionResult inserted =
                transaction.Execute(
                    "INSERT INTO job_requirements "
                    "(job_id, skill, level) "
                    "VALUES (?, ?, ?)",
                    {jobId,
                     requirement.skill,
                     static_cast<std::int64_t>(
                         requirement.level)});

            if (inserted.affectedRows != 1)
            {
                throw std::runtime_error(
                    "job requirement insert did not create a row");
            }
        }
    }

    void JobIngestionDao::SetReviewStatus(
        jobpilot::core::ConnectionPool::Transaction &transaction,
        std::uint64_t jobId,
        std::int32_t status)
    {
        if (jobId == 0)
        {
            throw std::invalid_argument(
                "job id must not be zero");
        }

        // 0 clean / 1 needs_review / 2 quarantined.
        if (status < 0 || status > 2)
        {
            throw std::invalid_argument(
                "review status must be 0, 1 or 2");
        }

        // MySQL reports 0 affected rows when the value is unchanged (the
        // default is already 0), so the result is intentionally not asserted.
        transaction.Execute(
            "UPDATE jobs SET review_status = ? WHERE id = ?",
            {static_cast<std::int64_t>(status),
             jobId});
    }

    void JobIngestionDao::InsertReviewItems(
        jobpilot::core::ConnectionPool::Transaction &transaction,
        std::uint64_t jobId,
        const std::vector<JobQualityFinding> &findings)
    {
        if (jobId == 0)
        {
            throw std::invalid_argument(
                "job id must not be zero");
        }

        // Re-observing a job refreshes its still-open findings; rows a reviewer
        // already resolved stay as the audit trail.
        transaction.Execute(
            "DELETE FROM job_review_items "
            "WHERE job_id = ? AND resolution = 'open'",
            {jobId});

        for (const JobQualityFinding &finding : findings)
        {
            const jobpilot::core::ExecutionResult inserted =
                transaction.Execute(
                    "INSERT INTO job_review_items "
                    "(job_id, reason_code, reason_text, outcome, resolution) "
                    "VALUES (?, ?, ?, ?, 'open')",
                    {jobId,
                     TruncateUtf8(finding.code, 64),
                     TruncateUtf8(finding.reason, 255),
                     std::string(ToString(finding.outcome))});

            if (inserted.affectedRows != 1)
            {
                throw std::runtime_error(
                    "job review item insert did not create a row");
            }
        }
    }

} // namespace jobpilot::service
