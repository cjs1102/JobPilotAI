#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace jobpilot::service
{
    inline constexpr const char *kJobMatchTaskType = "job_match";

    // 0013 lease defaults (ExecPlan 0013 section 6.2).
    inline constexpr std::int32_t kDefaultAiLeaseSeconds = 120;
    inline constexpr std::int32_t kDefaultAiMaxAttempts = 3;

    enum class AiTaskStatus : std::int32_t
    {
        kPending = 0,
        kProcessing = 1,
        kSuccess = 2,
        kFailed = 3,
    };

    inline bool IsTerminalAiTaskStatus(AiTaskStatus status)
    {
        return status == AiTaskStatus::kSuccess ||
               status == AiTaskStatus::kFailed;
    }

    struct AiTaskRecord
    {
        std::uint64_t id = 0;
        std::uint64_t userId = 0;
        std::string taskType;
        std::optional<std::uint64_t> jobId;
        std::optional<std::uint64_t> resumeId;
        AiTaskStatus status = AiTaskStatus::kPending;
        std::optional<std::string> result;
        std::optional<std::string> ruleResult;
        std::optional<std::string> errorMsg;
        std::int32_t retryCount = 0;
        std::string createTime;
        std::string updateTime;
    };

    struct AiTaskLeasePolicy
    {
        std::int32_t leaseSeconds = kDefaultAiLeaseSeconds;
        std::int32_t maxAttempts = kDefaultAiMaxAttempts;
    };

    struct AiTaskClaimOutcome
    {
        std::optional<AiTaskRecord> task;
        std::uint64_t requeuedStale = 0;
        std::uint64_t expiredFailed = 0;
    };

} // namespace jobpilot::service
