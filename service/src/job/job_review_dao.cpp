#include "jobpilot/service/job/job_review_dao.h"

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
        std::uint64_t ParseUnsigned(
            const jobpilot::core::Result::Value &value,
            std::uint64_t fallback = 0)
        {
            if (!value.has_value())
            {
                return fallback;
            }

            try
            {
                return std::stoull(value.value());
            }
            catch (...)
            {
                return fallback;
            }
        }

        std::int32_t ParseInt32(
            const jobpilot::core::Result::Value &value,
            std::int32_t fallback = 0)
        {
            if (!value.has_value())
            {
                return fallback;
            }

            try
            {
                return static_cast<std::int32_t>(
                    std::stol(value.value()));
            }
            catch (...)
            {
                return fallback;
            }
        }

        std::string ParseText(
            const jobpilot::core::Result::Value &value)
        {
            return value.has_value() ? value.value() : std::string();
        }

        std::optional<std::string> ParseOptionalText(
            const jobpilot::core::Result::Value &value)
        {
            if (!value.has_value())
            {
                return std::nullopt;
            }

            return value.value();
        }

        std::optional<std::uint32_t> ParseOptionalUnsigned32(
            const jobpilot::core::Result::Value &value)
        {
            if (!value.has_value())
            {
                return std::nullopt;
            }

            return static_cast<std::uint32_t>(
                std::stoul(value.value()));
        }

        jobpilot::core::SqlParam OptionalText(
            const std::optional<std::string> &value)
        {
            if (!value.has_value())
            {
                return nullptr;
            }

            return value.value();
        }

        // VARCHAR(255)/VARCHAR(64) count characters under utf8mb4, so cut on a
        // UTF-8 code-point boundary.
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

        const char *ResolutionFor(
            JobReviewDecision decision)
        {
            switch (decision)
            {
            case JobReviewDecision::kApprove:
                return "approved";
            case JobReviewDecision::kFix:
                return "fixed";
            case JobReviewDecision::kReject:
                return "rejected";
            }

            return "approved";
        }

        std::size_t ScalarCount(
            jobpilot::core::ConnectionPool &pool,
            const std::string &sql,
            const std::vector<jobpilot::core::SqlParam> &params)
        {
            const jobpilot::core::Result result =
                pool.Query(sql, params);

            if (result.rowCount() == 0)
            {
                return 0;
            }

            return static_cast<std::size_t>(
                ParseUnsigned(result.value(0, 0)));
        }
    } // namespace

    JobReviewDao::JobReviewDao(
        jobpilot::core::ConnectionPool &pool)
        : pool_(pool)
    {
    }

    std::size_t JobReviewDao::CountQueue(
        std::int32_t status)
    {
        return ScalarCount(
            pool_,
            "SELECT COUNT(*) FROM jobs WHERE review_status = ?",
            {static_cast<std::int64_t>(status)});
    }

    std::vector<JobReviewQueueItem> JobReviewDao::ListQueue(
        std::int32_t status,
        std::size_t limit,
        std::size_t offset)
    {
        const jobpilot::core::Result result =
            pool_.Query(
                "SELECT j.id, j.title, j.company, j.location, "
                "j.source_url, j.source, "
                "LEFT(COALESCE(j.description, ''), 200), "
                "j.review_status, j.create_time, "
                "COALESCE(i.reason_code, ''), "
                "COALESCE(i.reason_text, ''), "
                "COALESCE(i.outcome, ''), "
                "COALESCE(i.resolution, '') "
                "FROM jobs j "
                "LEFT JOIN job_review_items i ON i.id = ("
                "SELECT MAX(i2.id) FROM job_review_items i2 "
                "WHERE i2.job_id = j.id) "
                "WHERE j.review_status = ? "
                "ORDER BY j.id DESC LIMIT ? OFFSET ?",
                {static_cast<std::int64_t>(status),
                 static_cast<std::int64_t>(limit),
                 static_cast<std::int64_t>(offset)});

        std::vector<JobReviewQueueItem> items;
        items.reserve(result.rowCount());

        for (std::size_t row = 0;
             row < result.rowCount();
             ++row)
        {
            JobReviewQueueItem item;
            item.jobId = ParseUnsigned(result.value(row, 0));
            item.title = ParseText(result.value(row, 1));
            item.company = ParseText(result.value(row, 2));
            item.location = ParseOptionalText(result.value(row, 3));
            item.sourceUrl = ParseOptionalText(result.value(row, 4));
            item.source = ParseText(result.value(row, 5));
            item.descriptionExcerpt =
                ParseOptionalText(result.value(row, 6));
            item.reviewStatus =
                ParseInt32(result.value(row, 7), 1);
            item.createTime = ParseText(result.value(row, 8));
            item.reasonCode = ParseText(result.value(row, 9));
            item.reasonText = ParseText(result.value(row, 10));
            item.outcome = ParseText(result.value(row, 11));
            item.resolution = ParseText(result.value(row, 12));
            items.push_back(std::move(item));
        }

        return items;
    }

    std::optional<JobReviewState> JobReviewDao::FindState(
        std::uint64_t jobId)
    {
        const jobpilot::core::Result result =
            pool_.Query(
                "SELECT j.review_status, "
                "COALESCE(j.review_note, ''), "
                "COALESCE(j.reviewed_at, ''), "
                "COALESCE((SELECT i.resolution FROM job_review_items i "
                "WHERE i.job_id = j.id ORDER BY i.id DESC LIMIT 1), ''), "
                "j.company, j.title, COALESCE(j.source_url, ''), "
                "COALESCE(j.description, '') "
                "FROM jobs j WHERE j.id = ?",
                {jobId});

        if (result.rowCount() == 0)
        {
            return std::nullopt;
        }

        JobReviewState state;
        state.jobId = jobId;
        state.reviewStatus = ParseInt32(result.value(0, 0), 0);
        state.reviewNote = ParseText(result.value(0, 1));
        state.reviewedAt = ParseText(result.value(0, 2));
        state.lastResolution = ParseText(result.value(0, 3));
        state.company = ParseText(result.value(0, 4));
        state.title = ParseText(result.value(0, 5));
        state.sourceUrl = ParseText(result.value(0, 6));
        state.description = ParseText(result.value(0, 7));
        return state;
    }

    void JobReviewDao::ApplyDecision(
        std::uint64_t jobId,
        JobReviewDecision decision,
        const std::string &resolver,
        const std::string &note,
        const JobReviewFix &fix,
        const std::vector<JobReviewAuditField> &audit)
    {
        if (jobId == 0)
        {
            throw std::invalid_argument(
                "job id must not be zero");
        }

        auto transaction = pool_.BeginTransaction();

        const std::string resolution =
            ResolutionFor(decision);

        // Every still-open ingest finding of this row is resolved by the
        // decision; nothing is deleted (audit trail).
        transaction.Execute(
            "UPDATE job_review_items "
            "SET resolution = ?, resolved_by = ?, resolved_at = NOW() "
            "WHERE job_id = ? AND resolution = 'open'",
            {resolution,
             TruncateUtf8(resolver, 64),
             jobId});

        // Per-field before/after audit rows.
        for (const JobReviewAuditField &field : audit)
        {
            const std::string text =
                field.field + ": " +
                TruncateUtf8(field.before, 90) + " => " +
                TruncateUtf8(field.after, 90);

            const jobpilot::core::ExecutionResult inserted =
                transaction.Execute(
                    "INSERT INTO job_review_items "
                    "(job_id, reason_code, reason_text, outcome, "
                    "resolution, resolved_by, resolved_at) "
                    "VALUES (?, ?, ?, 'needs_review', ?, ?, NOW())",
                    {jobId,
                     "review_fix_" +
                         TruncateUtf8(field.field, 48),
                     TruncateUtf8(text, 255),
                     resolution,
                     TruncateUtf8(resolver, 64)});

            if (inserted.affectedRows != 1)
            {
                throw std::runtime_error(
                    "review audit insert did not create a row");
            }
        }

        const std::int32_t status =
            decision == JobReviewDecision::kReject ? 2 : 0;

        // COALESCE keeps the stored value when the fix does not provide it.
        transaction.Execute(
            "UPDATE jobs SET review_status = ?, reviewed_at = NOW(), "
            "review_note = ?, "
            "company = COALESCE(?, company), "
            "title = COALESCE(?, title), "
            "source_url = COALESCE(?, source_url), "
            "description = COALESCE(?, description) "
            "WHERE id = ?",
            {static_cast<std::int64_t>(status),
             TruncateUtf8(note, 255),
             OptionalText(fix.company),
             OptionalText(fix.title),
             OptionalText(fix.sourceUrl),
             OptionalText(fix.description),
             jobId});

        transaction.Commit();
    }

    std::size_t JobReviewDao::CountActiveJobs()
    {
        return ScalarCount(
            pool_,
            "SELECT COUNT(*) FROM jobs "
            "WHERE status = 1 AND closed_at IS NULL",
            {});
    }

    std::size_t JobReviewDao::CountByReviewStatus(
        std::int32_t status)
    {
        return CountQueue(status);
    }

    std::vector<JobReviewCandidate> JobReviewDao::ListActiveCandidates(
        std::size_t limit)
    {
        // Only untriaged clean rows are candidates: rows a reviewer already
        // queued or quarantined are never stomped by the historical pass, and
        // mode=revert can therefore restore the exact pre-apply distribution.
        const jobpilot::core::Result result =
            pool_.Query(
                "SELECT id, review_status, company, title, location, "
                "salary_min, salary_max, salary_currency, description, "
                "source, COALESCE(source_url, '') "
                "FROM jobs WHERE status = 1 AND closed_at IS NULL "
                "AND review_status = 0 "
                "ORDER BY id ASC LIMIT ?",
                {static_cast<std::int64_t>(limit)});

        std::vector<JobReviewCandidate> candidates;
        candidates.reserve(result.rowCount());

        for (std::size_t row = 0;
             row < result.rowCount();
             ++row)
        {
            JobReviewCandidate candidate;
            candidate.jobId = ParseUnsigned(result.value(row, 0));
            candidate.reviewStatus =
                ParseInt32(result.value(row, 1), 0);
            candidate.company = ParseText(result.value(row, 2));
            candidate.title = ParseText(result.value(row, 3));
            candidate.location =
                ParseOptionalText(result.value(row, 4));
            candidate.salaryMin =
                ParseOptionalUnsigned32(result.value(row, 5));
            candidate.salaryMax =
                ParseOptionalUnsigned32(result.value(row, 6));
            candidate.salaryCurrency =
                ParseText(result.value(row, 7));
            candidate.description =
                ParseOptionalText(result.value(row, 8));
            candidate.source = ParseText(result.value(row, 9));
            candidate.sourceUrl = ParseText(result.value(row, 10));
            candidates.push_back(std::move(candidate));
        }

        return candidates;
    }

    void JobReviewDao::ApplyBackfill(
        const std::vector<JobReviewBackfillEntry> &flagged,
        const std::string &actor)
    {
        auto transaction = pool_.BeginTransaction();

        for (const JobReviewBackfillEntry &entry : flagged)
        {
            if (entry.jobId == 0)
            {
                throw std::invalid_argument(
                    "backfill job id must not be zero");
            }

            transaction.Execute(
                "UPDATE jobs SET review_status = 1 WHERE id = ?",
                {entry.jobId});

            // Refresh only this run's own verdict rows; rows a reviewer
            // already resolved stay untouched.
            transaction.Execute(
                "DELETE FROM job_review_items "
                "WHERE job_id = ? AND resolution = 'open' "
                "AND resolved_by = 'backfill'",
                {entry.jobId});

            for (const JobQualityFinding &finding : entry.findings)
            {
                transaction.Execute(
                    "INSERT INTO job_review_items "
                    "(job_id, reason_code, reason_text, outcome, "
                    "resolution, resolved_by) "
                    "VALUES (?, ?, ?, ?, 'open', 'backfill')",
                    {entry.jobId,
                     TruncateUtf8(finding.code, 64),
                     TruncateUtf8(finding.reason, 255),
                     std::string(ToString(finding.outcome))});
            }
        }

        (void)actor;
        transaction.Commit();
    }

    std::size_t JobReviewDao::RevertBackfill()
    {
        const jobpilot::core::Result counted =
            pool_.Query(
                "SELECT COUNT(DISTINCT job_id) FROM job_review_items "
                "WHERE resolved_by = 'backfill' "
                "AND resolution = 'open'",
                {});

        const std::size_t jobs =
            counted.rowCount() == 0
                ? 0
                : static_cast<std::size_t>(
                      ParseUnsigned(counted.value(0, 0)));

        auto transaction = pool_.BeginTransaction();

        transaction.Execute(
            "UPDATE jobs j JOIN job_review_items i "
            "ON i.job_id = j.id "
            "SET j.review_status = 0 "
            "WHERE i.resolved_by = 'backfill' "
            "AND i.resolution = 'open'",
            {});

        transaction.Execute(
            "DELETE FROM job_review_items "
            "WHERE resolved_by = 'backfill' "
            "AND resolution = 'open'",
            {});

        transaction.Commit();

        return jobs;
    }

} // namespace jobpilot::service
