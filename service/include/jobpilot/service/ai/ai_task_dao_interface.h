#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "jobpilot/database/mysql_pool.h"
#include "jobpilot/service/ai/ai_task.h"

namespace jobpilot::service
{
    class IAiTaskDao
    {
    public:
        virtual ~IAiTaskDao() = default;

        // Creates a task. When resolveNow is true the deterministic result is
        // also the final result and the task is immediately successful, so no
        // worker is required (ExecPlan 0013 section 6.3).
        virtual std::optional<std::uint64_t> CreateTask(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t userId,
            const std::string &taskType,
            const std::optional<std::uint64_t> &jobId,
            const std::optional<std::uint64_t> &resumeId,
            const std::optional<std::string> &ruleResult,
            bool resolveNow) = 0;

        virtual std::optional<AiTaskRecord> FindOwnedTask(
            std::uint64_t userId,
            std::uint64_t taskId) = 0;

        // Idempotency lookup: the newest non-terminal task for the same
        // (user, job, resume) triple.
        virtual std::optional<AiTaskRecord> FindOpenJobMatchTask(
            std::uint64_t userId,
            std::uint64_t jobId,
            std::uint64_t resumeId) = 0;

        virtual std::optional<AiTaskRecord> FindTaskById(
            std::uint64_t taskId) = 0;

        // Atomic claim: requeue expired leases below the attempt cap, fail the
        // expired ones at or above it, then take the oldest pending task.
        virtual AiTaskClaimOutcome ClaimTask(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            const std::string &workerId,
            const AiTaskLeasePolicy &policy) = 0;

        virtual bool CompleteTask(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t taskId,
            const std::string &workerId,
            const std::string &result) = 0;

        // Returns the updated record, or nullopt when the task is not held by
        // this worker (wrong owner, not processing, or lease expired).
        virtual std::optional<AiTaskRecord> FailTask(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t taskId,
            const std::string &workerId,
            const std::string &errorMsg,
            bool retryable,
            const AiTaskLeasePolicy &policy) = 0;

        // Owner- and lease-checked read used by the internal input endpoint.
        virtual std::optional<AiTaskRecord> FindClaimedTask(
            std::uint64_t taskId,
            const std::string &workerId) = 0;

        // Pool-owning conveniences consumed by AiTaskService: they run the
        // matching transaction-taking method above inside one transaction, so
        // the service needs no database handle of its own (ExecPlan 0013
        // section 8: service tests use fake DAOs). No SQL is duplicated.

        // Idempotent creation: the existing non-terminal job_match task for
        // the same (user, job, resume) is returned, otherwise a new one is
        // inserted (ExecPlan 0013 section 5.2).
        virtual std::optional<AiTaskRecord> CreateJobMatchTaskIfAbsent(
            std::uint64_t userId,
            std::uint64_t jobId,
            std::uint64_t resumeId,
            const std::optional<std::string> &ruleResult,
            bool resolveNow) = 0;

        virtual AiTaskClaimOutcome ClaimNextTask(
            const std::string &workerId,
            const AiTaskLeasePolicy &policy) = 0;

        virtual std::optional<AiTaskRecord> CompleteOwnedTask(
            std::uint64_t taskId,
            const std::string &workerId,
            const std::string &result) = 0;

        virtual std::optional<AiTaskRecord> FailOwnedTask(
            std::uint64_t taskId,
            const std::string &workerId,
            const std::string &errorMsg,
            bool retryable,
            const AiTaskLeasePolicy &policy) = 0;
    };

} // namespace jobpilot::service
