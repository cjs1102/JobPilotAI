#include "jobpilot/service/ai/ai_task_service.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <vector>

#include "jobpilot/core/logger.h"
#include "jobpilot/service/ai/match_result.h"
#include "jobpilot/service/ai/match_result_json.h"
#include "jobpilot/service/ai/match_rule.h"

namespace jobpilot::service
{
    namespace
    {
        AiTaskView ToView(
            const AiTaskRecord &record)
        {
            AiTaskView view;
            view.taskId = record.id;
            view.taskType = record.taskType;
            view.status =
                static_cast<std::int32_t>(record.status);
            view.ruleResult = record.ruleResult;
            view.result = record.result;
            view.errorMsg = record.errorMsg;
            view.createdAt = record.createTime;
            view.updatedAt = record.updateTime;
            return view;
        }

        AiTaskClaim ToClaim(
            const AiTaskRecord &record)
        {
            AiTaskClaim claim;
            claim.taskId = record.id;
            claim.taskType = record.taskType;
            claim.jobId = record.jobId;
            claim.resumeId = record.resumeId;
            claim.retryCount = record.retryCount;
            return claim;
        }

        // ListVersions() is ordered by version, but the latest version is
        // selected explicitly so the matching input does not depend on the
        // query order (ExecPlan 0013 section 5.2).
        std::optional<ResumeVersionRecord> LatestVersion(
            const std::vector<ResumeVersionRecord> &versions)
        {
            std::optional<ResumeVersionRecord> latest;

            for (const ResumeVersionRecord &version : versions)
            {
                if (!latest.has_value() ||
                    version.version > latest->version)
                {
                    latest = version;
                }
            }

            return latest;
        }
    } // namespace

    AiTaskService::AiTaskService(
        IAiTaskDao &aiTaskDao,
        IJobDao &jobDao,
        IResumeDao &resumeDao,
        IUserProfileReader &profileReader,
        AiTaskConfig config)
        : aiTaskDao_(aiTaskDao),
          jobDao_(jobDao),
          resumeDao_(resumeDao),
          profileReader_(profileReader),
          config_(config)
    {
    }

    AiTaskLeasePolicy AiTaskService::LeasePolicy(
        const std::optional<std::int32_t> &leaseSeconds) const
    {
        const std::int32_t requested =
            leaseSeconds.has_value()
                ? leaseSeconds.value()
                : config_.leaseSeconds;

        AiTaskLeasePolicy policy;
        policy.leaseSeconds =
            std::min(
                kMaxAiLeaseSeconds,
                std::max(
                    kMinAiLeaseSeconds,
                    requested));
        policy.maxAttempts = config_.maxAttempts;
        return policy;
    }

    ServiceResult<AiTaskView> AiTaskService::CreateJobMatch(
        std::uint64_t userId,
        std::uint64_t jobId,
        std::uint64_t resumeId)
    {
        if (userId == 0 ||
            jobId == 0 ||
            resumeId == 0)
        {
            return ServiceResult<AiTaskView>{
                ErrorCode::kInvalidParam,
                {}};
        }

        try
        {
            const std::optional<JobDetail> job =
                jobDao_.FindActiveJobById(jobId);

            if (!job.has_value())
            {
                return ServiceResult<AiTaskView>{
                    ErrorCode::kJobNotFound,
                    {}};
            }

            const std::optional<ResumeRecord> resume =
                resumeDao_.FindOwnedResume(userId, resumeId);

            if (!resume.has_value())
            {
                return ServiceResult<AiTaskView>{
                    ErrorCode::kResumeNotFound,
                    {}};
            }

            const std::optional<ResumeVersionRecord> latest =
                LatestVersion(
                    resumeDao_.ListVersions(userId, resumeId));

            if (!latest.has_value())
            {
                return ServiceResult<AiTaskView>{
                    ErrorCode::kResumeNotFound,
                    {}};
            }

            // A version without text cannot be matched and must not produce
            // fabricated evidence (ExecPlan 0013 section 11).
            if (!latest->content.has_value() ||
                latest->content->empty())
            {
                return ServiceResult<AiTaskView>{
                    ErrorCode::kInvalidParam,
                    {}};
            }

            const std::vector<JobRequirement> requirements =
                jobDao_.ListRequirements(jobId);

            // match_rule_v1 drops the ranking-only recency component, so this
            // timestamp cannot influence the stored result (ExecPlan 0013
            // section 5.5).
            const MatchResult match = ScoreMatch(
                job->summary,
                requirements,
                profileReader_.FindProfile(userId),
                profileReader_.ListSkills(userId),
                latest->content.value(),
                std::chrono::system_clock::now());

            const std::string ruleResult =
                SerializeMatchResult(match);

            // Enhancement disabled (the default) means no worker is required:
            // the task is born successful with the rule baseline as its final
            // result (ExecPlan 0013 section 6.3).
            const bool resolveNow =
                !config_.enhancementEnabled;

            const std::optional<AiTaskRecord> task =
                aiTaskDao_.CreateJobMatchTaskIfAbsent(
                    userId,
                    jobId,
                    resumeId,
                    ruleResult,
                    resolveNow);

            if (!task.has_value())
            {
                return ServiceResult<AiTaskView>{
                    ErrorCode::kInternalError,
                    {}};
            }

            return ServiceResult<AiTaskView>{
                ErrorCode::kSuccess,
                ToView(task.value())};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "ai match creation failed: {}",
                error.what());

            return ServiceResult<AiTaskView>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

    ServiceResult<AiTaskView> AiTaskService::GetOwnedTask(
        std::uint64_t userId,
        std::uint64_t taskId)
    {
        if (userId == 0 || taskId == 0)
        {
            return ServiceResult<AiTaskView>{
                ErrorCode::kInvalidParam,
                {}};
        }

        try
        {
            const std::optional<AiTaskRecord> task =
                aiTaskDao_.FindOwnedTask(userId, taskId);

            if (!task.has_value())
            {
                return ServiceResult<AiTaskView>{
                    ErrorCode::kAiTaskNotFound,
                    {}};
            }

            return ServiceResult<AiTaskView>{
                ErrorCode::kSuccess,
                ToView(task.value())};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "ai task lookup failed: {}",
                error.what());

            return ServiceResult<AiTaskView>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

    ServiceResult<AiTaskClaimResult> AiTaskService::ClaimTask(
        const std::string &workerId,
        const std::optional<std::int32_t> &leaseSeconds)
    {
        if (workerId.empty())
        {
            return ServiceResult<AiTaskClaimResult>{
                ErrorCode::kInvalidParam,
                {}};
        }

        try
        {
            const AiTaskClaimOutcome outcome =
                aiTaskDao_.ClaimNextTask(
                    workerId,
                    LeasePolicy(leaseSeconds));

            AiTaskClaimResult result;
            result.requeuedStale = outcome.requeuedStale;
            result.expiredFailed = outcome.expiredFailed;

            if (outcome.task.has_value())
            {
                result.task =
                    ToClaim(outcome.task.value());
            }

            return ServiceResult<AiTaskClaimResult>{
                ErrorCode::kSuccess,
                result};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "ai task claim failed: {}",
                error.what());

            return ServiceResult<AiTaskClaimResult>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

    ServiceResult<AiTaskView> AiTaskService::CompleteTask(
        const std::string &workerId,
        std::uint64_t taskId,
        const std::string &resultJson)
    {
        if (workerId.empty() ||
            taskId == 0 ||
            resultJson.empty())
        {
            return ServiceResult<AiTaskView>{
                ErrorCode::kInvalidParam,
                {}};
        }

        try
        {
            const std::optional<AiTaskRecord> task =
                aiTaskDao_.CompleteOwnedTask(
                    taskId,
                    workerId,
                    resultJson);

            if (!task.has_value())
            {
                return ServiceResult<AiTaskView>{
                    ErrorCode::kAiTaskNotFound,
                    {}};
            }

            return ServiceResult<AiTaskView>{
                ErrorCode::kSuccess,
                ToView(task.value())};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "ai task completion failed: {}",
                error.what());

            return ServiceResult<AiTaskView>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

    ServiceResult<AiTaskView> AiTaskService::FailTask(
        const std::string &workerId,
        std::uint64_t taskId,
        const std::string &errorMsg,
        bool retryable)
    {
        if (workerId.empty() || taskId == 0)
        {
            return ServiceResult<AiTaskView>{
                ErrorCode::kInvalidParam,
                {}};
        }

        try
        {
            const std::optional<AiTaskRecord> task =
                aiTaskDao_.FailOwnedTask(
                    taskId,
                    workerId,
                    errorMsg,
                    retryable,
                    LeasePolicy());

            if (!task.has_value())
            {
                return ServiceResult<AiTaskView>{
                    ErrorCode::kAiTaskNotFound,
                    {}};
            }

            return ServiceResult<AiTaskView>{
                ErrorCode::kSuccess,
                ToView(task.value())};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "ai task failure recording failed: {}",
                error.what());

            return ServiceResult<AiTaskView>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

    ServiceResult<AiTaskInput> AiTaskService::BuildTaskInput(
        const std::string &workerId,
        std::uint64_t taskId)
    {
        if (workerId.empty() || taskId == 0)
        {
            return ServiceResult<AiTaskInput>{
                ErrorCode::kInvalidParam,
                {}};
        }

        try
        {
            // Ownership plus a live lease are enforced by the DAO query, so a
            // task that moved on returns the same not-found code as a missing
            // task (ExecPlan 0013 sections 5.3/5.4).
            const std::optional<AiTaskRecord> task =
                aiTaskDao_.FindClaimedTask(taskId, workerId);

            if (!task.has_value() ||
                !task->jobId.has_value() ||
                !task->resumeId.has_value())
            {
                return ServiceResult<AiTaskInput>{
                    ErrorCode::kAiTaskNotFound,
                    {}};
            }

            const std::optional<JobDetail> job =
                jobDao_.FindActiveJobById(task->jobId.value());

            const std::optional<ResumeRecord> resume =
                resumeDao_.FindOwnedResume(
                    task->userId,
                    task->resumeId.value());

            const std::optional<ResumeVersionRecord> latest =
                resume.has_value()
                    ? LatestVersion(
                          resumeDao_.ListVersions(
                              task->userId,
                              task->resumeId.value()))
                    : std::nullopt;

            if (!job.has_value() || !latest.has_value())
            {
                return ServiceResult<AiTaskInput>{
                    ErrorCode::kInternalError,
                    {}};
            }

            AiTaskInput input;
            input.task.taskId = task->id;
            input.task.taskType = task->taskType;
            input.task.retryCount = task->retryCount;

            input.job.summary = job->summary;
            input.job.description = job->description;
            input.job.requirements =
                jobDao_.ListRequirements(task->jobId.value());

            input.profile = profileReader_.FindProfile(task->userId);
            input.skills = profileReader_.ListSkills(task->userId);

            input.resume.resumeId = resume->id;
            input.resume.version = latest->version;
            input.resume.content =
                latest->content.value_or(std::string());

            input.ruleResult = task->ruleResult;

            return ServiceResult<AiTaskInput>{
                ErrorCode::kSuccess,
                input};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "ai task input build failed: {}",
                error.what());

            return ServiceResult<AiTaskInput>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

} // namespace jobpilot::service
