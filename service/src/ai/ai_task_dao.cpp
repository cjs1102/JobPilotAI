#include "jobpilot/service/ai/ai_task_dao.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

#include "jobpilot/database/mysql_pool.h"

namespace jobpilot::service
{
    namespace
    {
        // 0013 result schema (ExecPlan 0013 section 5.5).
        constexpr const char *kTaskColumns =
            "id, user_id, task_type, job_id, resume_id, status, result, "
            "rule_result, error_msg, retry_count, create_time, update_time";

        constexpr const char *kMaxAttemptsError =
            "lease expired after maximum attempts";

        std::optional<std::uint64_t> ParseUnsigned(
            const jobpilot::core::Result::Value &value)
        {
            if (!value.has_value())
            {
                return std::nullopt;
            }

            try
            {
                return std::stoull(value.value());
            }
            catch (...)
            {
                return std::nullopt;
            }
        }

        std::int32_t ParseInt32(
            const jobpilot::core::Result::Value &value,
            std::int32_t fallback)
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

        jobpilot::core::SqlParam OptionalUnsigned(
            const std::optional<std::uint64_t> &value)
        {
            if (!value.has_value())
            {
                return nullptr;
            }

            return value.value();
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

        AiTaskRecord ToRecord(
            const jobpilot::core::Result &result,
            std::size_t row)
        {
            AiTaskRecord record;
            record.id =
                ParseUnsigned(result.value(row, 0)).value_or(0);
            record.userId =
                ParseUnsigned(result.value(row, 1)).value_or(0);
            record.taskType = ParseText(result.value(row, 2));
            record.jobId = ParseUnsigned(result.value(row, 3));
            record.resumeId = ParseUnsigned(result.value(row, 4));
            record.status = static_cast<AiTaskStatus>(
                ParseInt32(result.value(row, 5), 0));
            record.result = ParseOptionalText(result.value(row, 6));
            record.ruleResult =
                ParseOptionalText(result.value(row, 7));
            record.errorMsg = ParseOptionalText(result.value(row, 8));
            record.retryCount =
                ParseInt32(result.value(row, 9), 0);
            record.createTime = ParseText(result.value(row, 10));
            record.updateTime = ParseText(result.value(row, 11));
            return record;
        }

        std::optional<AiTaskRecord> FirstRecord(
            const jobpilot::core::Result &result)
        {
            if (result.rowCount() == 0)
            {
                return std::nullopt;
            }

            return ToRecord(result, 0);
        }
    } // namespace

    AiTaskDao::AiTaskDao(
        jobpilot::core::ConnectionPool &pool)
        : pool_(pool)
    {
    }

    std::optional<std::uint64_t> AiTaskDao::CreateTask(
        jobpilot::core::ConnectionPool::Transaction &transaction,
        std::uint64_t userId,
        const std::string &taskType,
        const std::optional<std::uint64_t> &jobId,
        const std::optional<std::uint64_t> &resumeId,
        const std::optional<std::string> &ruleResult,
        bool resolveNow)
    {
        // A resolved-now task carries the deterministic result as its final
        // result, so it never needs a worker (ExecPlan 0013 section 6.3).
        const std::string statusValue =
            resolveNow ? "2" : "0";

        const jobpilot::core::SqlParam finalResult =
            resolveNow
                ? OptionalText(ruleResult)
                : jobpilot::core::SqlParam(nullptr);

        const jobpilot::core::ExecutionResult inserted =
            transaction.Execute(
                "INSERT INTO ai_tasks "
                "(user_id, task_type, job_id, resume_id, status, result, "
                "rule_result, error_msg, retry_count) "
                "VALUES (?, ?, ?, ?, " +
                    statusValue +
                    ", ?, ?, NULL, 0)",
                {userId,
                 taskType,
                 OptionalUnsigned(jobId),
                 OptionalUnsigned(resumeId),
                 finalResult,
                 OptionalText(ruleResult)});

        if (inserted.lastInsertId == 0)
        {
            throw std::runtime_error(
                "ai task insert returned no id");
        }

        return inserted.lastInsertId;
    }

    std::optional<AiTaskRecord> AiTaskDao::FindOwnedTask(
        std::uint64_t userId,
        std::uint64_t taskId)
    {
        const jobpilot::core::Result result = pool_.Query(
            "SELECT " + std::string(kTaskColumns) +
                " FROM ai_tasks WHERE id = ? AND user_id = ?",
            {taskId, userId});

        return FirstRecord(result);
    }

    std::optional<AiTaskRecord> AiTaskDao::FindOpenJobMatchTask(
        std::uint64_t userId,
        std::uint64_t jobId,
        std::uint64_t resumeId)
    {
        const jobpilot::core::Result result = pool_.Query(
            "SELECT " + std::string(kTaskColumns) +
                " FROM ai_tasks "
                "WHERE user_id = ? AND task_type = ? "
                "AND job_id = ? AND resume_id = ? "
                "AND status IN (0, 1) "
                "ORDER BY id DESC LIMIT 1",
            {userId,
             std::string(kJobMatchTaskType),
             jobId,
             resumeId});

        return FirstRecord(result);
    }

    std::optional<AiTaskRecord> AiTaskDao::FindTaskById(
        std::uint64_t taskId)
    {
        const jobpilot::core::Result result = pool_.Query(
            "SELECT " + std::string(kTaskColumns) +
                " FROM ai_tasks WHERE id = ?",
            {taskId});

        return FirstRecord(result);
    }

    AiTaskClaimOutcome AiTaskDao::ClaimTask(
        jobpilot::core::ConnectionPool::Transaction &transaction,
        const std::string &workerId,
        const AiTaskLeasePolicy &policy)
    {
        AiTaskClaimOutcome outcome;

        // A worker that died mid-task leaves an expired lease. Requeue it while
        // attempts remain; fail it once the cap is reached.
        const jobpilot::core::ExecutionResult requeued =
            transaction.Execute(
                "UPDATE ai_tasks "
                "SET status = 0, locked_by = NULL, lease_expire_at = NULL "
                "WHERE status = 1 AND lease_expire_at IS NOT NULL "
                "AND lease_expire_at < NOW() AND retry_count < ?",
                {static_cast<std::int64_t>(policy.maxAttempts)});

        outcome.requeuedStale = requeued.affectedRows;

        const jobpilot::core::ExecutionResult expired =
            transaction.Execute(
                "UPDATE ai_tasks "
                "SET status = 3, error_msg = ?, locked_by = NULL, "
                "lease_expire_at = NULL "
                "WHERE status = 1 AND lease_expire_at IS NOT NULL "
                "AND lease_expire_at < NOW() AND retry_count >= ?",
                {std::string(kMaxAttemptsError),
                 static_cast<std::int64_t>(policy.maxAttempts)});

        outcome.expiredFailed = expired.affectedRows;

        // The row lock is taken by this UPDATE itself, so two concurrent
        // workers can never win the same row.
        const jobpilot::core::ExecutionResult claimed =
            transaction.Execute(
                "UPDATE ai_tasks "
                "SET status = 1, locked_by = ?, "
                "lease_expire_at = TIMESTAMPADD(SECOND, ?, NOW()), "
                "retry_count = retry_count + 1 "
                "WHERE status = 0 ORDER BY id LIMIT 1",
                {workerId,
                 static_cast<std::int64_t>(policy.leaseSeconds)});

        if (claimed.affectedRows == 0)
        {
            return outcome;
        }

        const jobpilot::core::Result result = transaction.Query(
            "SELECT " + std::string(kTaskColumns) +
                " FROM ai_tasks "
                "WHERE locked_by = ? AND status = 1 "
                "ORDER BY id DESC LIMIT 1",
            {workerId});

        outcome.task = FirstRecord(result);

        return outcome;
    }

    bool AiTaskDao::CompleteTask(
        jobpilot::core::ConnectionPool::Transaction &transaction,
        std::uint64_t taskId,
        const std::string &workerId,
        const std::string &result)
    {
        const jobpilot::core::ExecutionResult updated =
            transaction.Execute(
                "UPDATE ai_tasks "
                "SET status = 2, result = ?, error_msg = NULL, "
                "locked_by = NULL, lease_expire_at = NULL "
                "WHERE id = ? AND locked_by = ? AND status = 1 "
                "AND (lease_expire_at IS NULL OR lease_expire_at >= NOW())",
                {result, taskId, workerId});

        return updated.affectedRows == 1;
    }

    std::optional<AiTaskRecord> AiTaskDao::FailTask(
        jobpilot::core::ConnectionPool::Transaction &transaction,
        std::uint64_t taskId,
        const std::string &workerId,
        const std::string &errorMsg,
        bool retryable,
        const AiTaskLeasePolicy &policy)
    {
        const jobpilot::core::Result current = transaction.Query(
            "SELECT " + std::string(kTaskColumns) +
                " FROM ai_tasks "
                "WHERE id = ? AND locked_by = ? AND status = 1 "
                "AND (lease_expire_at IS NULL OR lease_expire_at >= NOW()) "
                "FOR UPDATE",
            {taskId, workerId});

        const std::optional<AiTaskRecord> record =
            FirstRecord(current);

        if (!record.has_value())
        {
            return std::nullopt;
        }

        const bool requeue =
            retryable &&
            record->retryCount < policy.maxAttempts;

        transaction.Execute(
            requeue
                ? "UPDATE ai_tasks SET status = 0, error_msg = ?, "
                  "locked_by = NULL, lease_expire_at = NULL WHERE id = ?"
                : "UPDATE ai_tasks SET status = 3, error_msg = ?, "
                  "locked_by = NULL, lease_expire_at = NULL WHERE id = ?",
            {errorMsg, taskId});

        const jobpilot::core::Result updated = transaction.Query(
            "SELECT " + std::string(kTaskColumns) +
                " FROM ai_tasks WHERE id = ?",
            {taskId});

        return FirstRecord(updated);
    }

    std::optional<AiTaskRecord> AiTaskDao::FindClaimedTask(
        std::uint64_t taskId,
        const std::string &workerId)
    {
        const jobpilot::core::Result result = pool_.Query(
            "SELECT " + std::string(kTaskColumns) +
                " FROM ai_tasks "
                "WHERE id = ? AND locked_by = ? AND status = 1 "
                "AND (lease_expire_at IS NULL OR lease_expire_at >= NOW())",
            {taskId, workerId});

        return FirstRecord(result);
    }

    std::optional<AiTaskRecord> AiTaskDao::CreateJobMatchTaskIfAbsent(
        std::uint64_t userId,
        std::uint64_t jobId,
        std::uint64_t resumeId,
        const std::optional<std::string> &ruleResult,
        bool resolveNow)
    {
        // Idempotency first: an existing non-terminal task for the same triple
        // is returned untouched, so a retry never creates a duplicate
        // (ExecPlan 0013 section 5.2).
        const std::optional<AiTaskRecord> existing =
            FindOpenJobMatchTask(userId, jobId, resumeId);

        if (existing.has_value())
        {
            return existing;
        }

        jobpilot::core::ConnectionPool::Transaction transaction =
            pool_.BeginTransaction();

        const std::optional<std::uint64_t> created =
            CreateTask(
                transaction,
                userId,
                std::string(kJobMatchTaskType),
                jobId,
                resumeId,
                ruleResult,
                resolveNow);

        if (!created.has_value())
        {
            transaction.Rollback();
            return std::nullopt;
        }

        transaction.Commit();

        return FindTaskById(created.value());
    }

    AiTaskClaimOutcome AiTaskDao::ClaimNextTask(
        const std::string &workerId,
        const AiTaskLeasePolicy &policy)
    {
        jobpilot::core::ConnectionPool::Transaction transaction =
            pool_.BeginTransaction();

        const AiTaskClaimOutcome outcome =
            ClaimTask(transaction, workerId, policy);

        // Commit even when nothing was claimed: the same transaction requeues
        // expired leases and fails the rows at the attempt cap.
        transaction.Commit();

        return outcome;
    }

    std::optional<AiTaskRecord> AiTaskDao::CompleteOwnedTask(
        std::uint64_t taskId,
        const std::string &workerId,
        const std::string &result)
    {
        jobpilot::core::ConnectionPool::Transaction transaction =
            pool_.BeginTransaction();

        if (!CompleteTask(
                transaction,
                taskId,
                workerId,
                result))
        {
            transaction.Rollback();
            return std::nullopt;
        }

        transaction.Commit();

        return FindTaskById(taskId);
    }

    std::optional<AiTaskRecord> AiTaskDao::FailOwnedTask(
        std::uint64_t taskId,
        const std::string &workerId,
        const std::string &errorMsg,
        bool retryable,
        const AiTaskLeasePolicy &policy)
    {
        jobpilot::core::ConnectionPool::Transaction transaction =
            pool_.BeginTransaction();

        const std::optional<AiTaskRecord> failed =
            FailTask(
                transaction,
                taskId,
                workerId,
                errorMsg,
                retryable,
                policy);

        if (!failed.has_value())
        {
            transaction.Rollback();
            return std::nullopt;
        }

        transaction.Commit();

        return failed;
    }

} // namespace jobpilot::service
