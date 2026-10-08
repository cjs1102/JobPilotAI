#pragma once

#include "jobpilot/database/mysql_pool.h"
#include "jobpilot/service/ai/ai_task_dao_interface.h"

namespace jobpilot::service
{
    class AiTaskDao : public IAiTaskDao
    {
    public:
        explicit AiTaskDao(
            jobpilot::core::ConnectionPool &pool);

        std::optional<std::uint64_t> CreateTask(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t userId,
            const std::string &taskType,
            const std::optional<std::uint64_t> &jobId,
            const std::optional<std::uint64_t> &resumeId,
            const std::optional<std::string> &ruleResult,
            bool resolveNow) override;

        std::optional<AiTaskRecord> FindOwnedTask(
            std::uint64_t userId,
            std::uint64_t taskId) override;

        std::optional<AiTaskRecord> FindOpenJobMatchTask(
            std::uint64_t userId,
            std::uint64_t jobId,
            std::uint64_t resumeId) override;

        std::optional<AiTaskRecord> FindTaskById(
            std::uint64_t taskId) override;

        AiTaskClaimOutcome ClaimTask(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            const std::string &workerId,
            const AiTaskLeasePolicy &policy) override;

        bool CompleteTask(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t taskId,
            const std::string &workerId,
            const std::string &result) override;

        std::optional<AiTaskRecord> FailTask(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t taskId,
            const std::string &workerId,
            const std::string &errorMsg,
            bool retryable,
            const AiTaskLeasePolicy &policy) override;

        std::optional<AiTaskRecord> FindClaimedTask(
            std::uint64_t taskId,
            const std::string &workerId) override;

        std::optional<AiTaskRecord> CreateJobMatchTaskIfAbsent(
            std::uint64_t userId,
            std::uint64_t jobId,
            std::uint64_t resumeId,
            const std::optional<std::string> &ruleResult,
            bool resolveNow) override;

        AiTaskClaimOutcome ClaimNextTask(
            const std::string &workerId,
            const AiTaskLeasePolicy &policy) override;

        std::optional<AiTaskRecord> CompleteOwnedTask(
            std::uint64_t taskId,
            const std::string &workerId,
            const std::string &result) override;

        std::optional<AiTaskRecord> FailOwnedTask(
            std::uint64_t taskId,
            const std::string &workerId,
            const std::string &errorMsg,
            bool retryable,
            const AiTaskLeasePolicy &policy) override;

    private:
        jobpilot::core::ConnectionPool &pool_;
    };

} // namespace jobpilot::service
