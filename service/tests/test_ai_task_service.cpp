#include "jobpilot/service/ai/ai_task_service.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "jobpilot/database/mysql_pool.h"

namespace jobpilot::service
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::uint64_t kUserId = 90001;
        constexpr std::uint64_t kJobId = 20001;
        constexpr std::uint64_t kResumeId = 30001;

        using Triple = std::tuple<
            std::uint64_t,
            std::uint64_t,
            std::uint64_t>;

        Triple Key(
            std::uint64_t userId,
            std::uint64_t jobId,
            std::uint64_t resumeId)
        {
            return Triple{userId, jobId, resumeId};
        }

        // Simulates the AiTaskDao contract: an existing non-terminal task for
        // the same triple is returned, otherwise a new row is created. The
        // transaction-taking Step-3 methods must never be reached by the
        // service, so they only count calls.
        class FakeAiTaskDao final : public IAiTaskDao
        {
        public:
            std::optional<std::uint64_t> CreateTask(
                jobpilot::core::ConnectionPool::Transaction &,
                std::uint64_t,
                const std::string &,
                const std::optional<std::uint64_t> &,
                const std::optional<std::uint64_t> &,
                const std::optional<std::string> &,
                bool) override
            {
                ++transactionCalls;
                return std::nullopt;
            }

            std::optional<AiTaskRecord> FindOwnedTask(
                std::uint64_t userId,
                std::uint64_t taskId) override
            {
                ++findOwnedCalls;
                capturedFindOwnedUserId = userId;
                capturedFindOwnedTaskId = taskId;

                if (throwOnFindOwned)
                {
                    throw std::runtime_error(
                        "forced find failure");
                }

                const auto iterator = tasks.find(taskId);

                if (iterator == tasks.end() ||
                    iterator->second.userId != userId)
                {
                    return std::nullopt;
                }

                return iterator->second;
            }

            std::optional<AiTaskRecord> FindOpenJobMatchTask(
                std::uint64_t,
                std::uint64_t,
                std::uint64_t) override
            {
                return std::nullopt;
            }

            std::optional<AiTaskRecord> FindTaskById(
                std::uint64_t) override
            {
                return std::nullopt;
            }

            AiTaskClaimOutcome ClaimTask(
                jobpilot::core::ConnectionPool::Transaction &,
                const std::string &,
                const AiTaskLeasePolicy &) override
            {
                ++transactionCalls;
                return AiTaskClaimOutcome{};
            }

            bool CompleteTask(
                jobpilot::core::ConnectionPool::Transaction &,
                std::uint64_t,
                const std::string &,
                const std::string &) override
            {
                ++transactionCalls;
                return false;
            }

            std::optional<AiTaskRecord> FailTask(
                jobpilot::core::ConnectionPool::Transaction &,
                std::uint64_t,
                const std::string &,
                const std::string &,
                bool,
                const AiTaskLeasePolicy &) override
            {
                ++transactionCalls;
                return std::nullopt;
            }

            std::optional<AiTaskRecord> FindClaimedTask(
                std::uint64_t taskId,
                const std::string &workerId) override
            {
                ++findClaimedCalls;
                capturedClaimedTaskId = taskId;
                capturedClaimedWorkerId = workerId;

                if (throwOnFindClaimed)
                {
                    throw std::runtime_error(
                        "forced claimed lookup failure");
                }

                if (!claimedTask.has_value() ||
                    claimedTask->id != taskId ||
                    workerId != claimOwner)
                {
                    return std::nullopt;
                }

                return claimedRecord;
            }

            std::optional<AiTaskRecord> CreateJobMatchTaskIfAbsent(
                std::uint64_t userId,
                std::uint64_t jobId,
                std::uint64_t resumeId,
                const std::optional<std::string> &ruleResult,
                bool resolveNow) override
            {
                ++createCalls;
                capturedCreateUserId = userId;
                capturedCreateJobId = jobId;
                capturedCreateResumeId = resumeId;
                capturedRuleResult = ruleResult;
                capturedResolveNow = resolveNow;

                if (throwOnCreate)
                {
                    throw std::runtime_error(
                        "forced create failure");
                }

                const Triple key = Key(userId, jobId, resumeId);
                const auto existing = openTasks.find(key);

                if (existing != openTasks.end())
                {
                    ++reuseCalls;
                    return existing->second;
                }

                ++insertCalls;

                AiTaskRecord record;
                record.id = ++nextTaskId;
                record.userId = userId;
                record.taskType = std::string(kJobMatchTaskType);
                record.jobId = jobId;
                record.resumeId = resumeId;
                record.ruleResult = ruleResult;
                record.createTime = "2026-10-08 10:00:00";
                record.updateTime = record.createTime;

                if (resolveNow)
                {
                    // The DAO stores the deterministic baseline as the final
                    // result for a task that needs no worker.
                    record.status = AiTaskStatus::kSuccess;
                    record.result = ruleResult;
                }
                else
                {
                    record.status = AiTaskStatus::kPending;
                }

                openTasks[key] = record;
                tasks[record.id] = record;
                return record;
            }

            AiTaskClaimOutcome ClaimNextTask(
                const std::string &workerId,
                const AiTaskLeasePolicy &policy) override
            {
                ++claimCalls;
                capturedWorkerId = workerId;
                capturedPolicy = policy;

                if (throwOnClaim)
                {
                    throw std::runtime_error(
                        "forced claim failure");
                }

                return claimOutcome;
            }

            std::optional<AiTaskRecord> CompleteOwnedTask(
                std::uint64_t taskId,
                const std::string &workerId,
                const std::string &result) override
            {
                ++completeCalls;
                capturedCompleteTaskId = taskId;
                capturedCompleteWorkerId = workerId;
                capturedCompleteResult = result;

                if (throwOnComplete)
                {
                    throw std::runtime_error(
                        "forced completion failure");
                }

                return completeOutcome;
            }

            std::optional<AiTaskRecord> FailOwnedTask(
                std::uint64_t taskId,
                const std::string &workerId,
                const std::string &errorMsg,
                bool retryable,
                const AiTaskLeasePolicy &policy) override
            {
                ++failCalls;
                capturedFailTaskId = taskId;
                capturedFailWorkerId = workerId;
                capturedFailErrorMsg = errorMsg;
                capturedRetryable = retryable;
                capturedFailPolicy = policy;

                if (throwOnFail)
                {
                    throw std::runtime_error(
                        "forced failure recording failed");
                }

                return failOutcome;
            }

            void Put(const AiTaskRecord &record)
            {
                tasks[record.id] = record;
            }

            void SetClaimed(
                const AiTaskRecord &record,
                const std::string &owner)
            {
                claimedTask = record;
                claimedRecord = record;
                claimOwner = owner;
            }

            std::map<std::uint64_t, AiTaskRecord> tasks;
            std::map<Triple, AiTaskRecord> openTasks;
            std::optional<AiTaskRecord> claimedTask;
            std::optional<AiTaskRecord> claimedRecord;
            std::string claimOwner;

            AiTaskClaimOutcome claimOutcome;
            std::optional<AiTaskRecord> completeOutcome;
            std::optional<AiTaskRecord> failOutcome;

            std::size_t transactionCalls = 0;
            std::size_t createCalls = 0;
            std::size_t insertCalls = 0;
            std::size_t reuseCalls = 0;
            std::size_t findOwnedCalls = 0;
            std::size_t findClaimedCalls = 0;
            std::size_t claimCalls = 0;
            std::size_t completeCalls = 0;
            std::size_t failCalls = 0;

            std::uint64_t nextTaskId = 1000;
            std::uint64_t capturedCreateUserId = 0;
            std::uint64_t capturedCreateJobId = 0;
            std::uint64_t capturedCreateResumeId = 0;
            std::optional<std::string> capturedRuleResult;
            bool capturedResolveNow = false;
            std::uint64_t capturedFindOwnedUserId = 0;
            std::uint64_t capturedFindOwnedTaskId = 0;
            std::uint64_t capturedClaimedTaskId = 0;
            std::string capturedClaimedWorkerId;
            std::string capturedWorkerId;
            AiTaskLeasePolicy capturedPolicy;
            std::uint64_t capturedCompleteTaskId = 0;
            std::string capturedCompleteWorkerId;
            std::string capturedCompleteResult;
            std::uint64_t capturedFailTaskId = 0;
            std::string capturedFailWorkerId;
            std::string capturedFailErrorMsg;
            bool capturedRetryable = false;
            AiTaskLeasePolicy capturedFailPolicy;

            bool throwOnCreate = false;
            bool throwOnFindOwned = false;
            bool throwOnFindClaimed = false;
            bool throwOnClaim = false;
            bool throwOnComplete = false;
            bool throwOnFail = false;
        };

        class FakeJobDao final : public IJobDao
        {
        public:
            std::vector<JobSummary> SearchJobs(
                const JobSearchFilter &,
                std::size_t,
                std::size_t) override
            {
                return {};
            }

            std::size_t CountJobs(
                const JobSearchFilter &) override
            {
                return 0;
            }

            std::optional<JobDetail> FindActiveJobById(
                std::uint64_t jobId) override
            {
                ++detailCalls;
                capturedJobId = jobId;

                if (throwOnDetail)
                {
                    throw std::runtime_error(
                        "forced job lookup failure");
                }

                return detail;
            }

            bool ActiveJobExists(
                jobpilot::core::ConnectionPool::Transaction &,
                std::uint64_t) override
            {
                return true;
            }

            std::vector<JobRequirement> ListRequirements(
                std::uint64_t jobId) override
            {
                ++requirementCalls;
                capturedRequirementJobId = jobId;
                return requirements;
            }

            std::optional<JobDetail> detail;
            std::vector<JobRequirement> requirements;
            std::size_t detailCalls = 0;
            std::size_t requirementCalls = 0;
            std::uint64_t capturedJobId = 0;
            std::uint64_t capturedRequirementJobId = 0;
            bool throwOnDetail = false;
        };

        class FakeResumeDao final : public IResumeDao
        {
        public:
            std::optional<std::uint64_t> CreateResume(
                jobpilot::core::ConnectionPool::Transaction &,
                std::uint64_t,
                const std::string &) override
            {
                return std::nullopt;
            }

            bool LockOwnedResume(
                jobpilot::core::ConnectionPool::Transaction &,
                std::uint64_t,
                std::uint64_t) override
            {
                return false;
            }

            std::int32_t NextVersion(
                jobpilot::core::ConnectionPool::Transaction &,
                std::uint64_t) override
            {
                return 0;
            }

            std::optional<std::uint64_t> CreateVersion(
                jobpilot::core::ConnectionPool::Transaction &,
                std::uint64_t,
                std::int32_t,
                const std::optional<std::string> &,
                const std::optional<std::string> &) override
            {
                return std::nullopt;
            }

            std::vector<ResumeRecord> ListResumes(
                std::uint64_t,
                std::size_t,
                std::size_t) override
            {
                return {};
            }

            std::size_t CountResumes(
                std::uint64_t) override
            {
                return 0;
            }

            std::optional<ResumeRecord> FindOwnedResume(
                std::uint64_t userId,
                std::uint64_t resumeId) override
            {
                ++ownedCalls;
                capturedUserId = userId;
                capturedResumeId = resumeId;

                if (throwOnOwned)
                {
                    throw std::runtime_error(
                        "forced resume lookup failure");
                }

                return owned;
            }

            bool OwnedVersionExists(
                jobpilot::core::ConnectionPool::Transaction &,
                std::uint64_t,
                std::uint64_t) override
            {
                return false;
            }

            std::vector<ResumeVersionRecord> ListVersions(
                std::uint64_t userId,
                std::uint64_t resumeId) override
            {
                ++versionCalls;
                capturedVersionUserId = userId;
                capturedVersionResumeId = resumeId;

                if (throwOnVersions)
                {
                    throw std::runtime_error(
                        "forced version lookup failure");
                }

                return versions;
            }

            std::optional<ResumeRecord> owned;
            std::vector<ResumeVersionRecord> versions;
            std::size_t ownedCalls = 0;
            std::size_t versionCalls = 0;
            std::uint64_t capturedUserId = 0;
            std::uint64_t capturedResumeId = 0;
            std::uint64_t capturedVersionUserId = 0;
            std::uint64_t capturedVersionResumeId = 0;
            bool throwOnOwned = false;
            bool throwOnVersions = false;
        };

        class FakeProfileReader final : public IUserProfileReader
        {
        public:
            std::optional<UserProfile> FindProfile(
                std::uint64_t userId) override
            {
                ++profileCalls;
                capturedUserId = userId;
                return profile;
            }

            std::vector<UserSkill> ListSkills(
                std::uint64_t userId) override
            {
                ++skillCalls;
                capturedSkillUserId = userId;
                return skills;
            }

            std::optional<UserProfile> profile;
            std::vector<UserSkill> skills;
            std::size_t profileCalls = 0;
            std::size_t skillCalls = 0;
            std::uint64_t capturedUserId = 0;
            std::uint64_t capturedSkillUserId = 0;
        };

        JobDetail MakeJobDetail()
        {
            JobDetail detail;
            detail.summary.id = kJobId;
            detail.summary.company = "Example";
            detail.summary.title = "C++ Backend Engineer";
            detail.summary.location = "Shenzhen";
            detail.summary.salaryMin = 20;
            detail.summary.salaryMax = 35;
            detail.summary.salaryCurrency = "CNY";
            detail.summary.source = "manual";
            detail.description = "Build backend services";
            return detail;
        }

        ResumeRecord MakeResume(
            std::uint64_t userId = kUserId,
            std::uint64_t resumeId = kResumeId)
        {
            ResumeRecord record;
            record.id = resumeId;
            record.userId = userId;
            record.title = "Backend resume";
            record.latestVersion = 2;
            record.createTime = "2026-10-01 10:00:00";
            record.updateTime = record.createTime;
            return record;
        }

        std::vector<ResumeVersionRecord> MakeVersions(
            std::uint64_t resumeId = kResumeId)
        {
            ResumeVersionRecord first;
            first.id = 1;
            first.resumeId = resumeId;
            first.version = 1;
            first.content = "C++ only";

            ResumeVersionRecord second;
            second.id = 2;
            second.resumeId = resumeId;
            second.version = 2;
            second.content = "C++ Redis Linux";

            return {first, second};
        }

        UserProfile MakeProfile()
        {
            UserProfile profile;
            profile.userId = kUserId;
            profile.targetPosition = "C++ Backend";
            profile.location = "Shenzhen";
            profile.salaryMin = 20;
            profile.salaryMax = 30;
            profile.summary = "Backend engineer";
            return profile;
        }

        AiTaskRecord MakeTaskRecord(
            std::uint64_t taskId,
            AiTaskStatus status,
            const std::optional<std::string> &ruleResult,
            const std::optional<std::string> &result,
            const std::optional<std::string> &errorMsg =
                std::nullopt)
        {
            AiTaskRecord record;
            record.id = taskId;
            record.userId = kUserId;
            record.taskType = std::string(kJobMatchTaskType);
            record.jobId = kJobId;
            record.resumeId = kResumeId;
            record.status = status;
            record.ruleResult = ruleResult;
            record.result = result;
            record.errorMsg = errorMsg;
            record.retryCount = 1;
            record.createTime = "2026-10-08 10:00:00";
            record.updateTime = "2026-10-08 10:00:05";
            return record;
        }

        const char *kRuleResultJson =
            R"({"score_version":"match_rule_v1","score":82.0})";

        // Wires a valid job/resume/profile so only the aspect under test needs
        // extra configuration.
        void ConfigureValidMatch(
            FakeJobDao &jobDao,
            FakeResumeDao &resumeDao,
            FakeProfileReader &profileReader)
        {
            jobDao.detail = MakeJobDetail();
            jobDao.requirements = {
                JobRequirement{"C++", 3},
                JobRequirement{"Redis", 2}};
            resumeDao.owned = MakeResume();
            resumeDao.versions = MakeVersions();
            profileReader.profile = MakeProfile();
            profileReader.skills = {
                UserSkill{1, kUserId, "C++", 3, ""},
                UserSkill{2, kUserId, "Redis", 2, ""}};
        }
    } // namespace

    TEST(AiTaskServiceTest, RejectsInvalidIdentifiersWithoutDaoCalls)
    {
        FakeAiTaskDao aiTaskDao;
        FakeJobDao jobDao;
        FakeResumeDao resumeDao;
        FakeProfileReader profileReader;
        AiTaskService service(
            aiTaskDao,
            jobDao,
            resumeDao,
            profileReader);

        EXPECT_EQ(
            service.CreateJobMatch(0, kJobId, kResumeId).code,
            ErrorCode::kInvalidParam);
        EXPECT_EQ(
            service.CreateJobMatch(kUserId, 0, kResumeId).code,
            ErrorCode::kInvalidParam);
        EXPECT_EQ(
            service.CreateJobMatch(kUserId, kJobId, 0).code,
            ErrorCode::kInvalidParam);

        EXPECT_EQ(
            service.GetOwnedTask(0, 1).code,
            ErrorCode::kInvalidParam);
        EXPECT_EQ(
            service.GetOwnedTask(kUserId, 0).code,
            ErrorCode::kInvalidParam);

        EXPECT_EQ(
            service.ClaimTask("").code,
            ErrorCode::kInvalidParam);
        EXPECT_EQ(
            service.CompleteTask("", 1, "{}").code,
            ErrorCode::kInvalidParam);
        EXPECT_EQ(
            service.CompleteTask("worker-a", 0, "{}").code,
            ErrorCode::kInvalidParam);
        EXPECT_EQ(
            service.CompleteTask("worker-a", 1, "").code,
            ErrorCode::kInvalidParam);
        EXPECT_EQ(
            service.FailTask("", 1, "boom", true).code,
            ErrorCode::kInvalidParam);
        EXPECT_EQ(
            service.FailTask("worker-a", 0, "boom", true).code,
            ErrorCode::kInvalidParam);
        EXPECT_EQ(
            service.BuildTaskInput("", 1).code,
            ErrorCode::kInvalidParam);
        EXPECT_EQ(
            service.BuildTaskInput("worker-a", 0).code,
            ErrorCode::kInvalidParam);

        EXPECT_EQ(jobDao.detailCalls, 0U);
        EXPECT_EQ(resumeDao.ownedCalls, 0U);
        EXPECT_EQ(aiTaskDao.createCalls, 0U);
        EXPECT_EQ(aiTaskDao.claimCalls, 0U);
        EXPECT_EQ(aiTaskDao.completeCalls, 0U);
        EXPECT_EQ(aiTaskDao.failCalls, 0U);
    }

    TEST(AiTaskServiceTest, RejectsMissingJobResumeAndResumeText)
    {
        FakeAiTaskDao aiTaskDao;
        FakeJobDao jobDao;
        FakeResumeDao resumeDao;
        FakeProfileReader profileReader;
        AiTaskService service(
            aiTaskDao,
            jobDao,
            resumeDao,
            profileReader);

        // Missing or inactive job -> 40001, resume not owned -> 40002
        // (ExecPlan 0013 section 5.2).
        EXPECT_EQ(
            service.CreateJobMatch(
                       kUserId,
                       kJobId,
                       kResumeId)
                .code,
            ErrorCode::kJobNotFound);
        EXPECT_EQ(aiTaskDao.createCalls, 0U);

        ConfigureValidMatch(
            jobDao,
            resumeDao,
            profileReader);

        resumeDao.owned.reset();

        EXPECT_EQ(
            service.CreateJobMatch(
                       kUserId,
                       kJobId,
                       kResumeId)
                .code,
            ErrorCode::kResumeNotFound);
        EXPECT_EQ(aiTaskDao.createCalls, 0U);

        resumeDao.owned = MakeResume();
        resumeDao.versions.clear();

        EXPECT_EQ(
            service.CreateJobMatch(
                       kUserId,
                       kJobId,
                       kResumeId)
                .code,
            ErrorCode::kResumeNotFound);
        EXPECT_EQ(aiTaskDao.createCalls, 0U);

        // A version without text is a parameter problem, not a missing
        // resource.
        resumeDao.versions = MakeVersions();
        resumeDao.versions[1].content.reset();

        EXPECT_EQ(
            service.CreateJobMatch(
                       kUserId,
                       kJobId,
                       kResumeId)
                .code,
            ErrorCode::kInvalidParam);
        EXPECT_EQ(aiTaskDao.createCalls, 0U);

        resumeDao.versions[1].content = "";

        EXPECT_EQ(
            service.CreateJobMatch(
                       kUserId,
                       kJobId,
                       kResumeId)
                .code,
            ErrorCode::kInvalidParam);
        EXPECT_EQ(aiTaskDao.createCalls, 0U);
    }

    TEST(AiTaskServiceTest, MapsDaoFailuresToDatabaseError)
    {
        FakeAiTaskDao aiTaskDao;
        FakeJobDao jobDao;
        FakeResumeDao resumeDao;
        FakeProfileReader profileReader;
        ConfigureValidMatch(
            jobDao,
            resumeDao,
            profileReader);
        AiTaskService service(
            aiTaskDao,
            jobDao,
            resumeDao,
            profileReader);

        jobDao.throwOnDetail = true;

        EXPECT_EQ(
            service.CreateJobMatch(
                       kUserId,
                       kJobId,
                       kResumeId)
                .code,
            ErrorCode::kDatabaseError);

        jobDao.throwOnDetail = false;
        aiTaskDao.throwOnCreate = true;

        EXPECT_EQ(
            service.CreateJobMatch(
                       kUserId,
                       kJobId,
                       kResumeId)
                .code,
            ErrorCode::kDatabaseError);

        aiTaskDao.throwOnCreate = false;
        aiTaskDao.throwOnFindOwned = true;

        EXPECT_EQ(
            service.GetOwnedTask(kUserId, 1).code,
            ErrorCode::kDatabaseError);

        aiTaskDao.throwOnFindOwned = false;
        aiTaskDao.throwOnClaim = true;

        EXPECT_EQ(
            service.ClaimTask("worker-a").code,
            ErrorCode::kDatabaseError);

        aiTaskDao.throwOnClaim = false;
        aiTaskDao.throwOnComplete = true;

        EXPECT_EQ(
            service.CompleteTask("worker-a", 1, "{}").code,
            ErrorCode::kDatabaseError);

        aiTaskDao.throwOnComplete = false;
        aiTaskDao.throwOnFail = true;

        EXPECT_EQ(
            service.FailTask("worker-a", 1, "boom", true).code,
            ErrorCode::kDatabaseError);

        aiTaskDao.throwOnFail = false;
        aiTaskDao.throwOnFindClaimed = true;

        EXPECT_EQ(
            service.BuildTaskInput("worker-a", 1).code,
            ErrorCode::kDatabaseError);
    }

    TEST(AiTaskServiceTest, DisabledEnhancementResolvesAtCreation)
    {
        FakeAiTaskDao aiTaskDao;
        FakeJobDao jobDao;
        FakeResumeDao resumeDao;
        FakeProfileReader profileReader;
        ConfigureValidMatch(
            jobDao,
            resumeDao,
            profileReader);
        AiTaskService service(
            aiTaskDao,
            jobDao,
            resumeDao,
            profileReader);

        const ServiceResult<AiTaskView> result =
            service.CreateJobMatch(
                kUserId,
                kJobId,
                kResumeId);

        ASSERT_EQ(result.code, ErrorCode::kSuccess);

        EXPECT_TRUE(aiTaskDao.capturedResolveNow);
        EXPECT_EQ(aiTaskDao.createCalls, 1U);
        EXPECT_EQ(aiTaskDao.reuseCalls, 0U);
        EXPECT_EQ(aiTaskDao.transactionCalls, 0U);

        EXPECT_EQ(result.value.taskId, 1001U);
        EXPECT_EQ(result.value.taskType, kJobMatchTaskType);
        EXPECT_EQ(result.value.status, 2);
        EXPECT_FALSE(result.value.errorMsg.has_value());

        // Degradation: the final result is exactly the deterministic baseline.
        ASSERT_TRUE(result.value.ruleResult.has_value());
        ASSERT_TRUE(result.value.result.has_value());
        EXPECT_EQ(
            result.value.result.value(),
            result.value.ruleResult.value());

        const Json rule = Json::parse(
            result.value.ruleResult.value());

        EXPECT_EQ(
            rule["score_version"].get<std::string>(),
            "match_rule_v1");
        EXPECT_EQ(
            rule["source"].get<std::string>(),
            "rule");
        EXPECT_EQ(
            rule["matched_skills"],
            Json({"C++", "Redis"}));
        EXPECT_EQ(
            rule["missing_skills"],
            Json::array());

        // The latest resume version (v2) is the matching input: the older v1
        // does not mention Redis.
        ASSERT_EQ(rule["resume_evidence"].size(), 2U);
        EXPECT_TRUE(
            rule["resume_evidence"][1]["mentioned"].get<bool>());

        EXPECT_EQ(jobDao.capturedJobId, kJobId);
        EXPECT_EQ(jobDao.capturedRequirementJobId, kJobId);
        EXPECT_EQ(resumeDao.capturedUserId, kUserId);
        EXPECT_EQ(resumeDao.capturedResumeId, kResumeId);
        EXPECT_EQ(profileReader.capturedUserId, kUserId);
        EXPECT_EQ(profileReader.capturedSkillUserId, kUserId);
    }

    TEST(AiTaskServiceTest, EnabledEnhancementLeavesTheTaskPending)
    {
        FakeAiTaskDao aiTaskDao;
        FakeJobDao jobDao;
        FakeResumeDao resumeDao;
        FakeProfileReader profileReader;
        ConfigureValidMatch(
            jobDao,
            resumeDao,
            profileReader);

        AiTaskConfig config;
        config.enhancementEnabled = true;

        AiTaskService service(
            aiTaskDao,
            jobDao,
            resumeDao,
            profileReader,
            config);

        const ServiceResult<AiTaskView> result =
            service.CreateJobMatch(
                kUserId,
                kJobId,
                kResumeId);

        ASSERT_EQ(result.code, ErrorCode::kSuccess);
        EXPECT_FALSE(aiTaskDao.capturedResolveNow);

        EXPECT_EQ(result.value.status, 0);
        EXPECT_FALSE(result.value.result.has_value());
        ASSERT_TRUE(result.value.ruleResult.has_value());

        const Json rule = Json::parse(
            result.value.ruleResult.value());

        EXPECT_EQ(
            rule["score_version"].get<std::string>(),
            "match_rule_v1");
    }

    TEST(AiTaskServiceTest, ReusesTheOpenTaskForTheSameTriple)
    {
        FakeAiTaskDao aiTaskDao;
        FakeJobDao jobDao;
        FakeResumeDao resumeDao;
        FakeProfileReader profileReader;
        ConfigureValidMatch(
            jobDao,
            resumeDao,
            profileReader);

        AiTaskConfig config;
        config.enhancementEnabled = true;

        AiTaskService service(
            aiTaskDao,
            jobDao,
            resumeDao,
            profileReader,
            config);

        const ServiceResult<AiTaskView> first =
            service.CreateJobMatch(kUserId, kJobId, kResumeId);
        const ServiceResult<AiTaskView> second =
            service.CreateJobMatch(kUserId, kJobId, kResumeId);

        ASSERT_EQ(first.code, ErrorCode::kSuccess);
        ASSERT_EQ(second.code, ErrorCode::kSuccess);
        EXPECT_EQ(first.value.taskId, second.value.taskId);
        EXPECT_EQ(aiTaskDao.createCalls, 2U);
        EXPECT_EQ(aiTaskDao.insertCalls, 1U);
        EXPECT_EQ(aiTaskDao.reuseCalls, 1U);
        EXPECT_EQ(
            aiTaskDao.capturedCreateUserId,
            kUserId);
        EXPECT_EQ(aiTaskDao.capturedCreateJobId, kJobId);
        EXPECT_EQ(
            aiTaskDao.capturedCreateResumeId,
            kResumeId);
    }

    TEST(AiTaskServiceTest, OwnershipRefusalUsesOneNotFoundCode)
    {
        FakeAiTaskDao aiTaskDao;
        FakeJobDao jobDao;
        FakeResumeDao resumeDao;
        FakeProfileReader profileReader;
        AiTaskService service(
            aiTaskDao,
            jobDao,
            resumeDao,
            profileReader);

        aiTaskDao.Put(
            MakeTaskRecord(
                1024,
                AiTaskStatus::kPending,
                kRuleResultJson,
                std::nullopt));

        const ServiceResult<AiTaskView> owned =
            service.GetOwnedTask(kUserId, 1024);

        ASSERT_EQ(owned.code, ErrorCode::kSuccess);
        EXPECT_EQ(owned.value.taskId, 1024U);
        EXPECT_EQ(owned.value.status, 0);
        ASSERT_TRUE(owned.value.ruleResult.has_value());
        EXPECT_EQ(
            owned.value.ruleResult.value(),
            kRuleResultJson);
        EXPECT_FALSE(owned.value.result.has_value());

        // A task that exists but belongs to somebody else is indistinguishable
        // from a missing task (ExecPlan 0013 section 5.2).
        EXPECT_EQ(
            service.GetOwnedTask(424242, 1024).code,
            ErrorCode::kAiTaskNotFound);
        EXPECT_EQ(
            service.GetOwnedTask(kUserId, 2048).code,
            ErrorCode::kAiTaskNotFound);
    }

    TEST(AiTaskServiceTest, FailedAndDegradedTasksStillExposeTheRuleResult)
    {
        FakeAiTaskDao aiTaskDao;
        FakeJobDao jobDao;
        FakeResumeDao resumeDao;
        FakeProfileReader profileReader;
        AiTaskService service(
            aiTaskDao,
            jobDao,
            resumeDao,
            profileReader);

        aiTaskDao.Put(
            MakeTaskRecord(
                2048,
                AiTaskStatus::kFailed,
                kRuleResultJson,
                std::nullopt,
                "max attempts exceeded"));

        const ServiceResult<AiTaskView> failed =
            service.GetOwnedTask(kUserId, 2048);

        ASSERT_EQ(failed.code, ErrorCode::kSuccess);
        EXPECT_EQ(failed.value.status, 3);
        EXPECT_FALSE(failed.value.result.has_value());
        ASSERT_TRUE(failed.value.errorMsg.has_value());
        EXPECT_EQ(
            failed.value.errorMsg.value(),
            "max attempts exceeded");
        ASSERT_TRUE(failed.value.ruleResult.has_value());
        EXPECT_EQ(
            failed.value.ruleResult.value(),
            kRuleResultJson);
    }

    TEST(AiTaskServiceTest, ClaimDelegatesAndAppliesConfiguredPolicy)
    {
        FakeAiTaskDao aiTaskDao;
        FakeJobDao jobDao;
        FakeResumeDao resumeDao;
        FakeProfileReader profileReader;

        AiTaskConfig config;
        config.leaseSeconds = 45;
        config.maxAttempts = 7;

        AiTaskService service(
            aiTaskDao,
            jobDao,
            resumeDao,
            profileReader,
            config);

        const ServiceResult<AiTaskClaimResult> empty =
            service.ClaimTask("worker-a");

        ASSERT_EQ(empty.code, ErrorCode::kSuccess);
        EXPECT_FALSE(empty.value.task.has_value());
        EXPECT_EQ(aiTaskDao.claimCalls, 1U);
        EXPECT_EQ(aiTaskDao.capturedWorkerId, "worker-a");
        EXPECT_EQ(aiTaskDao.capturedPolicy.leaseSeconds, 45);
        EXPECT_EQ(aiTaskDao.capturedPolicy.maxAttempts, 7);
        EXPECT_EQ(aiTaskDao.transactionCalls, 0U);

        // A requested lease_seconds wins over the configured default but is
        // clamped into [1, 3600]; max_attempts always comes from config.
        ASSERT_EQ(
            service.ClaimTask("worker-a", std::int32_t(300)).code,
            ErrorCode::kSuccess);
        EXPECT_EQ(aiTaskDao.capturedPolicy.leaseSeconds, 300);
        EXPECT_EQ(aiTaskDao.capturedPolicy.maxAttempts, 7);

        ASSERT_EQ(
            service.ClaimTask("worker-a", std::int32_t(0)).code,
            ErrorCode::kSuccess);
        EXPECT_EQ(aiTaskDao.capturedPolicy.leaseSeconds, 1);

        ASSERT_EQ(
            service.ClaimTask("worker-a", std::int32_t(-5)).code,
            ErrorCode::kSuccess);
        EXPECT_EQ(aiTaskDao.capturedPolicy.leaseSeconds, 1);

        ASSERT_EQ(
            service
                .ClaimTask("worker-a", std::int32_t(1000000))
                .code,
            ErrorCode::kSuccess);
        EXPECT_EQ(aiTaskDao.capturedPolicy.leaseSeconds, 3600);

        AiTaskClaimOutcome outcome;
        outcome.task = MakeTaskRecord(
            1024,
            AiTaskStatus::kProcessing,
            kRuleResultJson,
            std::nullopt);
        outcome.requeuedStale = 2;
        outcome.expiredFailed = 1;
        aiTaskDao.claimOutcome = outcome;

        const ServiceResult<AiTaskClaimResult> claimed =
            service.ClaimTask("worker-a");

        ASSERT_EQ(claimed.code, ErrorCode::kSuccess);
        ASSERT_TRUE(claimed.value.task.has_value());
        EXPECT_EQ(claimed.value.task->taskId, 1024U);
        EXPECT_EQ(
            claimed.value.task->taskType,
            kJobMatchTaskType);
        ASSERT_TRUE(claimed.value.task->jobId.has_value());
        EXPECT_EQ(
            claimed.value.task->jobId.value(),
            kJobId);
        ASSERT_TRUE(claimed.value.task->resumeId.has_value());
        EXPECT_EQ(
            claimed.value.task->resumeId.value(),
            kResumeId);
        EXPECT_EQ(claimed.value.task->retryCount, 1);
        EXPECT_EQ(claimed.value.requeuedStale, 2U);
        EXPECT_EQ(claimed.value.expiredFailed, 1U);
    }

    TEST(AiTaskServiceTest, CompleteAndFailDelegateWithOwnerRefusal)
    {
        FakeAiTaskDao aiTaskDao;
        FakeJobDao jobDao;
        FakeResumeDao resumeDao;
        FakeProfileReader profileReader;

        AiTaskConfig config;
        config.leaseSeconds = 30;
        config.maxAttempts = 2;

        AiTaskService service(
            aiTaskDao,
            jobDao,
            resumeDao,
            profileReader,
            config);

        // Wrong owner or expired lease: the DAO returns nothing and the
        // service reports the same not-found code (ExecPlan 0013 section 5.3).
        EXPECT_EQ(
            service
                .CompleteTask("worker-b", 1024, "{\"score\":90}")
                .code,
            ErrorCode::kAiTaskNotFound);
        EXPECT_EQ(
            service
                .FailTask("worker-b", 1024, "provider timeout", true)
                .code,
            ErrorCode::kAiTaskNotFound);

        EXPECT_EQ(aiTaskDao.completeCalls, 1U);
        EXPECT_EQ(aiTaskDao.capturedCompleteTaskId, 1024U);
        EXPECT_EQ(
            aiTaskDao.capturedCompleteWorkerId,
            "worker-b");
        EXPECT_EQ(
            aiTaskDao.capturedCompleteResult,
            "{\"score\":90}");
        EXPECT_EQ(aiTaskDao.failCalls, 1U);
        EXPECT_EQ(aiTaskDao.capturedFailTaskId, 1024U);
        EXPECT_EQ(
            aiTaskDao.capturedFailWorkerId,
            "worker-b");
        EXPECT_EQ(
            aiTaskDao.capturedFailErrorMsg,
            "provider timeout");
        EXPECT_TRUE(aiTaskDao.capturedRetryable);
        EXPECT_EQ(
            aiTaskDao.capturedFailPolicy.leaseSeconds,
            30);
        EXPECT_EQ(
            aiTaskDao.capturedFailPolicy.maxAttempts,
            2);

        aiTaskDao.completeOutcome = MakeTaskRecord(
            1024,
            AiTaskStatus::kSuccess,
            kRuleResultJson,
            "{\"score\":90,\"ai\":{\"degraded\":true}}");

        const ServiceResult<AiTaskView> completed =
            service.CompleteTask(
                "worker-a",
                1024,
                "{\"score\":90,\"ai\":{\"degraded\":true}}");

        ASSERT_EQ(completed.code, ErrorCode::kSuccess);
        EXPECT_EQ(completed.value.status, 2);
        ASSERT_TRUE(completed.value.result.has_value());
        EXPECT_EQ(
            completed.value.result.value(),
            "{\"score\":90,\"ai\":{\"degraded\":true}}");
        ASSERT_TRUE(completed.value.ruleResult.has_value());
        EXPECT_EQ(
            completed.value.ruleResult.value(),
            kRuleResultJson);

        aiTaskDao.failOutcome = MakeTaskRecord(
            1024,
            AiTaskStatus::kPending,
            kRuleResultJson,
            std::nullopt,
            "provider timeout");

        const ServiceResult<AiTaskView> requeued =
            service.FailTask(
                "worker-a",
                1024,
                "provider timeout",
                true);

        ASSERT_EQ(requeued.code, ErrorCode::kSuccess);
        EXPECT_EQ(requeued.value.status, 0);
        ASSERT_TRUE(requeued.value.errorMsg.has_value());
        EXPECT_EQ(
            requeued.value.errorMsg.value(),
            "provider timeout");
        ASSERT_TRUE(requeued.value.ruleResult.has_value());

        aiTaskDao.failOutcome = MakeTaskRecord(
            1024,
            AiTaskStatus::kFailed,
            kRuleResultJson,
            std::nullopt,
            "invalid provider response");

        const ServiceResult<AiTaskView> terminated =
            service.FailTask(
                "worker-a",
                1024,
                "invalid provider response",
                false);

        ASSERT_EQ(terminated.code, ErrorCode::kSuccess);
        EXPECT_EQ(terminated.value.status, 3);
        EXPECT_FALSE(terminated.value.result.has_value());
        ASSERT_TRUE(terminated.value.ruleResult.has_value());
        EXPECT_EQ(
            terminated.value.ruleResult.value(),
            kRuleResultJson);
    }

    TEST(AiTaskServiceTest, BuildsTheWorkerInputBundle)
    {
        FakeAiTaskDao aiTaskDao;
        FakeJobDao jobDao;
        FakeResumeDao resumeDao;
        FakeProfileReader profileReader;

        AiTaskRecord claimed = MakeTaskRecord(
            1024,
            AiTaskStatus::kProcessing,
            kRuleResultJson,
            std::nullopt);
        claimed.retryCount = 2;
        aiTaskDao.SetClaimed(claimed, "worker-a");

        jobDao.detail = MakeJobDetail();
        jobDao.requirements = {
            JobRequirement{"C++", 3},
            JobRequirement{"Redis", 2}};
        resumeDao.owned = MakeResume();
        resumeDao.versions = MakeVersions();
        profileReader.profile = MakeProfile();
        profileReader.skills = {
            UserSkill{1, kUserId, "C++", 3, ""}};

        AiTaskService service(
            aiTaskDao,
            jobDao,
            resumeDao,
            profileReader);

        const ServiceResult<AiTaskInput> result =
            service.BuildTaskInput("worker-a", 1024);

        ASSERT_EQ(result.code, ErrorCode::kSuccess);

        EXPECT_EQ(aiTaskDao.capturedClaimedTaskId, 1024U);
        EXPECT_EQ(
            aiTaskDao.capturedClaimedWorkerId,
            "worker-a");

        EXPECT_EQ(result.value.task.taskId, 1024U);
        EXPECT_EQ(
            result.value.task.taskType,
            kJobMatchTaskType);
        EXPECT_EQ(result.value.task.retryCount, 2);

        EXPECT_EQ(result.value.job.summary.id, kJobId);
        EXPECT_EQ(
            result.value.job.summary.company,
            "Example");
        EXPECT_EQ(
            result.value.job.summary.title,
            "C++ Backend Engineer");
        ASSERT_TRUE(
            result.value.job.summary.location.has_value());
        EXPECT_EQ(
            result.value.job.summary.location.value(),
            "Shenzhen");
        ASSERT_TRUE(
            result.value.job.summary.salaryMin.has_value());
        EXPECT_EQ(
            result.value.job.summary.salaryMin.value(),
            20U);
        ASSERT_TRUE(
            result.value.job.summary.salaryMax.has_value());
        EXPECT_EQ(
            result.value.job.summary.salaryMax.value(),
            35U);
        EXPECT_EQ(
            result.value.job.summary.salaryCurrency,
            "CNY");
        ASSERT_TRUE(
            result.value.job.description.has_value());
        EXPECT_EQ(
            result.value.job.description.value(),
            "Build backend services");
        ASSERT_EQ(
            result.value.job.requirements.size(),
            2U);
        EXPECT_EQ(
            result.value.job.requirements[1].skill,
            "Redis");
        EXPECT_EQ(
            result.value.job.requirements[1].level,
            2);

        ASSERT_TRUE(result.value.profile.has_value());
        ASSERT_TRUE(
            result.value.profile->targetPosition.has_value());
        EXPECT_EQ(
            result.value.profile->targetPosition.value(),
            "C++ Backend");
        ASSERT_EQ(result.value.skills.size(), 1U);
        EXPECT_EQ(result.value.skills[0].name, "C++");

        EXPECT_EQ(result.value.resume.resumeId, kResumeId);
        EXPECT_EQ(result.value.resume.version, 2);
        EXPECT_EQ(
            result.value.resume.content,
            "C++ Redis Linux");

        ASSERT_TRUE(result.value.ruleResult.has_value());
        EXPECT_EQ(
            result.value.ruleResult.value(),
            kRuleResultJson);
    }

    TEST(AiTaskServiceTest, RefusesInputWithoutALiveLease)
    {
        FakeAiTaskDao aiTaskDao;
        FakeJobDao jobDao;
        FakeResumeDao resumeDao;
        FakeProfileReader profileReader;

        AiTaskRecord claimed = MakeTaskRecord(
            1024,
            AiTaskStatus::kProcessing,
            kRuleResultJson,
            std::nullopt);
        aiTaskDao.SetClaimed(claimed, "worker-a");

        jobDao.detail = MakeJobDetail();
        resumeDao.owned = MakeResume();
        resumeDao.versions = MakeVersions();

        AiTaskService service(
            aiTaskDao,
            jobDao,
            resumeDao,
            profileReader);

        EXPECT_EQ(
            service.BuildTaskInput("worker-b", 1024).code,
            ErrorCode::kAiTaskNotFound);
        EXPECT_EQ(
            service.BuildTaskInput("worker-a", 2048).code,
            ErrorCode::kAiTaskNotFound);
        EXPECT_EQ(jobDao.detailCalls, 0U);
        EXPECT_EQ(resumeDao.ownedCalls, 0U);

        EXPECT_EQ(
            service.BuildTaskInput("worker-a", 1024).code,
            ErrorCode::kSuccess);
        EXPECT_EQ(jobDao.detailCalls, 1U);
    }

    TEST(AiTaskServiceTest, ReportsBrokenBundleAsInternalError)
    {
        FakeAiTaskDao aiTaskDao;
        FakeJobDao jobDao;
        FakeResumeDao resumeDao;
        FakeProfileReader profileReader;

        AiTaskRecord claimed = MakeTaskRecord(
            1024,
            AiTaskStatus::kProcessing,
            kRuleResultJson,
            std::nullopt);
        aiTaskDao.SetClaimed(claimed, "worker-a");

        resumeDao.owned = MakeResume();
        resumeDao.versions = MakeVersions();

        AiTaskService service(
            aiTaskDao,
            jobDao,
            resumeDao,
            profileReader);

        // The job referenced by the lease no longer resolves (closed or
        // deleted), so no worker input can be built.
        EXPECT_EQ(
            service.BuildTaskInput("worker-a", 1024).code,
            ErrorCode::kInternalError);

        // Same when the resume vanished but the job is still active.
        jobDao.detail = MakeJobDetail();
        resumeDao.owned.reset();

        EXPECT_EQ(
            service.BuildTaskInput("worker-a", 1024).code,
            ErrorCode::kInternalError);

        // A live lease with a complete bundle still succeeds.
        resumeDao.owned = MakeResume();

        EXPECT_EQ(
            service.BuildTaskInput("worker-a", 1024).code,
            ErrorCode::kSuccess);
    }

} // namespace jobpilot::service
