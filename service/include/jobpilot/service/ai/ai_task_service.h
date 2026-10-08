#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "jobpilot/service/ai/ai_task.h"
#include "jobpilot/service/ai/ai_task_dao_interface.h"
#include "jobpilot/service/job/job_dao_interface.h"
#include "jobpilot/service/resume/resume_dao_interface.h"
#include "jobpilot/service/result.h"
#include "jobpilot/service/user/user_dao_interface.h"

namespace jobpilot::service
{
    // Lease bounds: a requested lease_seconds is clamped into this range so a
    // worker can never hold a task indefinitely (0013 Step 5).
    inline constexpr std::int32_t kMinAiLeaseSeconds = 1;
    inline constexpr std::int32_t kMaxAiLeaseSeconds = 3600;

    // 0013 AI configuration, supplied by the caller (config.yaml + environment
    // overrides). The service never reads globals or the environment and never
    // holds a provider key (ExecPlan 0013 section 6.2).
    struct AiTaskConfig
    {
        bool enhancementEnabled = false;
        std::int32_t leaseSeconds = kDefaultAiLeaseSeconds;
        std::int32_t maxAttempts = kDefaultAiMaxAttempts;
    };

    // Public task projection (ExecPlan 0013 section 5.2). rule_result and
    // result stay raw JSON so the deterministic baseline is byte-identical to
    // what the worker merged.
    struct AiTaskView
    {
        std::uint64_t taskId = 0;
        std::string taskType;
        std::int32_t status = 0;
        std::optional<std::string> ruleResult;
        std::optional<std::string> result;
        std::optional<std::string> errorMsg;
        std::string createdAt;
        std::string updatedAt;
    };

    // Claim payload (ExecPlan 0013 section 5.3).
    struct AiTaskClaim
    {
        std::uint64_t taskId = 0;
        std::string taskType;
        std::optional<std::uint64_t> jobId;
        std::optional<std::uint64_t> resumeId;
        std::int32_t retryCount = 0;
    };

    struct AiTaskClaimResult
    {
        // Absent when the queue is empty.
        std::optional<AiTaskClaim> task;
        std::uint64_t requeuedStale = 0;
        std::uint64_t expiredFailed = 0;
    };

    // Input bundle returned to the worker (ExecPlan 0013 section 5.4) so it
    // never reads business tables (ADR-0009; ADR-0006 superseded).
    struct AiTaskInputTask
    {
        std::uint64_t taskId = 0;
        std::string taskType;
        std::int32_t retryCount = 0;
    };

    struct AiTaskInputJob
    {
        JobSummary summary;
        std::optional<std::string> description;
        std::vector<JobRequirement> requirements;
    };

    struct AiTaskInputResume
    {
        std::uint64_t resumeId = 0;
        std::int32_t version = 0;
        std::string content;
    };

    struct AiTaskInput
    {
        AiTaskInputTask task;
        AiTaskInputJob job;
        std::optional<UserProfile> profile;
        std::vector<UserSkill> skills;
        AiTaskInputResume resume;
        std::optional<std::string> ruleResult;
    };

    class AiTaskService
    {
    public:
        AiTaskService(
            IAiTaskDao &aiTaskDao,
            IJobDao &jobDao,
            IResumeDao &resumeDao,
            IUserProfileReader &profileReader,
            AiTaskConfig config = AiTaskConfig{});

        // POST /api/v1/ai/match. Validates the job and resume, computes the
        // deterministic match_rule_v1 result, stores it as rule_result and
        // either resolves the task immediately (enhancement disabled) or
        // leaves it pending for a worker (ExecPlan 0013 sections 5.2/6.3).
        ServiceResult<AiTaskView> CreateJobMatch(
            std::uint64_t userId,
            std::uint64_t jobId,
            std::uint64_t resumeId);

        // GET /api/v1/ai/tasks/{id}. A missing task and a task owned by
        // somebody else both return kAiTaskNotFound, so existence does not
        // leak (ExecPlan 0013 section 5.2).
        ServiceResult<AiTaskView> GetOwnedTask(
            std::uint64_t userId,
            std::uint64_t taskId);

        // POST /internal/ai/tasks/claim (ExecPlan 0013 section 5.3). An
        // optional lease_seconds overrides the configured lease for this
        // claim only, clamped into [kMinAiLeaseSeconds, kMaxAiLeaseSeconds];
        // when absent the configured lease is used (clamped the same way).
        // max_attempts always comes from the configuration.
        ServiceResult<AiTaskClaimResult> ClaimTask(
            const std::string &workerId,
            const std::optional<std::int32_t> &leaseSeconds =
                std::nullopt);

        // POST /internal/ai/tasks/{id}/complete (ExecPlan 0013 section 5.3).
        ServiceResult<AiTaskView> CompleteTask(
            const std::string &workerId,
            std::uint64_t taskId,
            const std::string &resultJson);

        // POST /internal/ai/tasks/{id}/fail (ExecPlan 0013 section 5.3).
        ServiceResult<AiTaskView> FailTask(
            const std::string &workerId,
            std::uint64_t taskId,
            const std::string &errorMsg,
            bool retryable);

        // GET /internal/ai/tasks/{id}/input (ExecPlan 0013 section 5.4).
        // Refused with kAiTaskNotFound unless this worker holds a live lease.
        ServiceResult<AiTaskInput> BuildTaskInput(
            const std::string &workerId,
            std::uint64_t taskId);

    private:
        AiTaskLeasePolicy LeasePolicy(
            const std::optional<std::int32_t> &leaseSeconds =
                std::nullopt) const;

        IAiTaskDao &aiTaskDao_;
        IJobDao &jobDao_;
        IResumeDao &resumeDao_;
        IUserProfileReader &profileReader_;
        AiTaskConfig config_;
    };

} // namespace jobpilot::service
