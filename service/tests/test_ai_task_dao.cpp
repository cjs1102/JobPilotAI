#include "jobpilot/service/ai/ai_task_dao.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <unistd.h>

#include "jobpilot/database/mysql_pool.h"
#include "jobpilot/service/user/user_dao.h"
#include "test_user_support.h"

namespace jobpilot::service
{
    class AiTaskDaoTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            pool = jobpilot::test::MakeConnectionPool();
            userDao = std::make_unique<UserDao>(*pool);
            dao = std::make_unique<AiTaskDao>(*pool);
            email = jobpilot::test::UniqueEmail(
                "ai-task-dao");

            const std::optional<std::uint64_t> created =
                userDao->CreateUser(
                    "ai-task-dao",
                    email,
                    "hash");

            ASSERT_TRUE(created.has_value());
            userId = created.value();
        }

        void TearDown() override
        {
            CleanupTasks();
            jobpilot::test::CleanupUser(
                pool.get(),
                email);
        }

        void CleanupTasks()
        {
            pool->Execute(
                "DELETE FROM ai_tasks WHERE user_id = ?",
                {userId});
        }

        static std::uint64_t UniqueNumber()
        {
            static std::atomic<std::uint64_t> counter{0};

            return 7000000000ULL +
                   static_cast<std::uint64_t>(::getpid()) *
                       1000ULL +
                   counter.fetch_add(1);
        }

        std::optional<std::uint64_t> CreateTask(
            bool resolveNow,
            const std::string &ruleResult = "{\"score\":42}")
        {
            const std::uint64_t jobId = UniqueNumber();
            const std::uint64_t resumeId = UniqueNumber();

            jobpilot::core::ConnectionPool::Transaction transaction =
                pool->BeginTransaction();

            const std::optional<std::uint64_t> taskId =
                dao->CreateTask(
                    transaction,
                    userId,
                    std::string(kJobMatchTaskType),
                    jobId,
                    resumeId,
                    ruleResult,
                    resolveNow);

            transaction.Commit();

            return taskId;
        }

        AiTaskClaimOutcome Claim(
            const std::string &workerId,
            const AiTaskLeasePolicy &policy = AiTaskLeasePolicy{})
        {
            jobpilot::core::ConnectionPool::Transaction transaction =
                pool->BeginTransaction();

            const AiTaskClaimOutcome outcome =
                dao->ClaimTask(transaction, workerId, policy);

            transaction.Commit();

            return outcome;
        }

        void ExpireLease(std::uint64_t taskId)
        {
            pool->Execute(
                "UPDATE ai_tasks "
                "SET lease_expire_at = TIMESTAMPADD(SECOND, -30, NOW()) "
                "WHERE id = ?",
                {taskId});
        }

        std::unique_ptr<jobpilot::core::ConnectionPool> pool;
        std::unique_ptr<UserDao> userDao;
        std::unique_ptr<AiTaskDao> dao;
        std::uint64_t userId = 0;
        std::string email;
    };

    TEST_F(AiTaskDaoTest, CreatesPendingAndResolvedTasks)
    {
        const std::optional<std::uint64_t> pending =
            CreateTask(false);

        ASSERT_TRUE(pending.has_value());

        const std::optional<AiTaskRecord> pendingRecord =
            dao->FindOwnedTask(userId, pending.value());

        ASSERT_TRUE(pendingRecord.has_value());
        EXPECT_EQ(
            pendingRecord->status,
            AiTaskStatus::kPending);
        EXPECT_EQ(pendingRecord->retryCount, 0);
        EXPECT_FALSE(pendingRecord->result.has_value());
        ASSERT_TRUE(pendingRecord->ruleResult.has_value());
        EXPECT_EQ(
            pendingRecord->ruleResult.value(),
            "{\"score\": 42}");

        const std::optional<std::uint64_t> resolved =
            CreateTask(true, "{\"score\": 7}");

        ASSERT_TRUE(resolved.has_value());

        const std::optional<AiTaskRecord> resolvedRecord =
            dao->FindOwnedTask(userId, resolved.value());

        ASSERT_TRUE(resolvedRecord.has_value());
        EXPECT_EQ(
            resolvedRecord->status,
            AiTaskStatus::kSuccess);
        ASSERT_TRUE(resolvedRecord->result.has_value());
        EXPECT_EQ(resolvedRecord->result.value(), "{\"score\": 7}");
    }

    TEST_F(AiTaskDaoTest, EnforcesTaskOwnership)
    {
        const std::optional<std::uint64_t> taskId =
            CreateTask(false);

        ASSERT_TRUE(taskId.has_value());

        const std::string otherEmail =
            jobpilot::test::UniqueEmail("ai-task-other");

        const std::optional<std::uint64_t> otherUser =
            userDao->CreateUser(
                "ai-task-other",
                otherEmail,
                "hash");

        ASSERT_TRUE(otherUser.has_value());

        EXPECT_FALSE(
            dao->FindOwnedTask(
                 otherUser.value(),
                 taskId.value())
                .has_value());
        EXPECT_TRUE(
            dao->FindOwnedTask(userId, taskId.value())
                .has_value());

        jobpilot::test::CleanupUser(pool.get(), otherEmail);
    }

    TEST_F(AiTaskDaoTest, ReusesOnlyOpenJobMatchTask)
    {
        const std::uint64_t jobId = UniqueNumber();
        const std::uint64_t resumeId = UniqueNumber();

        jobpilot::core::ConnectionPool::Transaction transaction =
            pool->BeginTransaction();

        const std::optional<std::uint64_t> pendingId =
            dao->CreateTask(
                transaction,
                userId,
                std::string(kJobMatchTaskType),
                jobId,
                resumeId,
                "{\"score\": 1}",
                false);

        transaction.Commit();

        ASSERT_TRUE(pendingId.has_value());

        const std::optional<AiTaskRecord> open =
            dao->FindOpenJobMatchTask(userId, jobId, resumeId);

        ASSERT_TRUE(open.has_value());
        EXPECT_EQ(open->id, pendingId.value());

        EXPECT_FALSE(
            dao->FindOpenJobMatchTask(
                 userId,
                 jobId,
                 UniqueNumber())
                .has_value());

        const std::optional<std::uint64_t> settled =
            CreateTask(true);

        ASSERT_TRUE(settled.has_value());

        const std::optional<AiTaskRecord> settledRecord =
            dao->FindOwnedTask(userId, settled.value());

        ASSERT_TRUE(settledRecord.has_value());
        EXPECT_FALSE(
            dao->FindOpenJobMatchTask(
                 userId,
                 settledRecord->jobId.value(),
                 settledRecord->resumeId.value())
                .has_value());
    }

    TEST_F(AiTaskDaoTest, ClaimsOldestPendingTaskAndHoldsLease)
    {
        const std::optional<std::uint64_t> first = CreateTask(false);
        const std::optional<std::uint64_t> second = CreateTask(false);

        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());

        const AiTaskClaimOutcome claimed = Claim("worker-a");

        ASSERT_TRUE(claimed.task.has_value());
        EXPECT_EQ(claimed.task->id, first.value());
        EXPECT_EQ(claimed.task->retryCount, 1);
        EXPECT_EQ(claimed.requeuedStale, 0U);
        EXPECT_EQ(claimed.expiredFailed, 0U);

        EXPECT_TRUE(
            dao->FindClaimedTask(first.value(), "worker-a")
                .has_value());
        EXPECT_FALSE(
            dao->FindClaimedTask(first.value(), "worker-b")
                .has_value());

        const AiTaskClaimOutcome next = Claim("worker-b");

        ASSERT_TRUE(next.task.has_value());
        EXPECT_EQ(next.task->id, second.value());

        EXPECT_FALSE(Claim("worker-c").task.has_value());
    }

    TEST_F(AiTaskDaoTest, NeverHandsTheSameTaskToTwoWorkers)
    {
        constexpr int kTasks = 4;
        constexpr int kWorkers = 3;

        for (int index = 0; index < kTasks; ++index)
        {
            ASSERT_TRUE(CreateTask(false).has_value());
        }

        std::mutex mutex;
        std::vector<std::uint64_t> claimedIds;
        std::atomic<int> failedClaims{0};

        auto worker = [&](const std::string &workerId)
        {
            for (int attempt = 0; attempt < kTasks; ++attempt)
            {
                try
                {
                    jobpilot::core::ConnectionPool::Transaction
                        transaction = pool->BeginTransaction();
                    const AiTaskClaimOutcome outcome =
                        dao->ClaimTask(
                            transaction,
                            workerId,
                            AiTaskLeasePolicy{});
                    transaction.Commit();

                    if (!outcome.task.has_value())
                    {
                        continue;
                    }

                    std::lock_guard<std::mutex> lock(mutex);
                    claimedIds.push_back(outcome.task->id);
                }
                catch (const std::exception &)
                {
                    failedClaims.fetch_add(1);
                }
            }
        };

        std::vector<std::thread> threads;

        for (int index = 0; index < kWorkers; ++index)
        {
            threads.emplace_back(
                worker,
                "worker-" + std::to_string(index));
        }

        for (std::thread &thread : threads)
        {
            thread.join();
        }

        EXPECT_EQ(claimedIds.size(), static_cast<std::size_t>(kTasks));

        for (std::size_t left = 0; left < claimedIds.size(); ++left)
        {
            for (std::size_t right = left + 1;
                 right < claimedIds.size();
                 ++right)
            {
                EXPECT_NE(claimedIds[left], claimedIds[right]);
            }
        }
    }

    TEST_F(AiTaskDaoTest, ReclaimsExpiredLeaseForAnotherWorker)
    {
        const std::optional<std::uint64_t> taskId = CreateTask(false);

        ASSERT_TRUE(taskId.has_value());

        AiTaskLeasePolicy shortLease;
        shortLease.leaseSeconds = 1;

        const AiTaskClaimOutcome first =
            Claim("worker-a", shortLease);

        ASSERT_TRUE(first.task.has_value());
        EXPECT_EQ(first.task->id, taskId.value());

        ExpireLease(taskId.value());

        const AiTaskClaimOutcome recovered = Claim("worker-b");

        ASSERT_TRUE(recovered.task.has_value());
        EXPECT_EQ(recovered.task->id, taskId.value());
        EXPECT_EQ(recovered.task->retryCount, 2);
        EXPECT_EQ(recovered.requeuedStale, 1U);

        EXPECT_TRUE(
            dao->FindClaimedTask(taskId.value(), "worker-b")
                .has_value());
        EXPECT_FALSE(
            dao->FindClaimedTask(taskId.value(), "worker-a")
                .has_value());
    }

    TEST_F(AiTaskDaoTest, FailsExpiredLeaseBeyondAttemptCap)
    {
        const std::optional<std::uint64_t> taskId = CreateTask(false);

        ASSERT_TRUE(taskId.has_value());

        AiTaskLeasePolicy singleAttempt;
        singleAttempt.leaseSeconds = 1;
        singleAttempt.maxAttempts = 1;

        const AiTaskClaimOutcome first =
            Claim("worker-a", singleAttempt);

        ASSERT_TRUE(first.task.has_value());
        EXPECT_EQ(first.task->retryCount, 1);

        ExpireLease(taskId.value());

        const AiTaskClaimOutcome afterCap =
            Claim("worker-b", singleAttempt);

        EXPECT_FALSE(afterCap.task.has_value());
        EXPECT_EQ(afterCap.expiredFailed, 1U);

        const std::optional<AiTaskRecord> failed =
            dao->FindOwnedTask(userId, taskId.value());

        ASSERT_TRUE(failed.has_value());
        EXPECT_EQ(failed->status, AiTaskStatus::kFailed);
        ASSERT_TRUE(failed->errorMsg.has_value());
    }

    TEST_F(AiTaskDaoTest, RequeuesRetryableFailureBelowTheCap)
    {
        const std::optional<std::uint64_t> taskId = CreateTask(false);

        ASSERT_TRUE(taskId.has_value());

        const AiTaskClaimOutcome claimed = Claim("worker-a");

        ASSERT_TRUE(claimed.task.has_value());

        jobpilot::core::ConnectionPool::Transaction transaction =
            pool->BeginTransaction();

        const std::optional<AiTaskRecord> requeued =
            dao->FailTask(
                transaction,
                taskId.value(),
                "worker-a",
                "provider timeout",
                true,
                AiTaskLeasePolicy{});

        transaction.Commit();

        ASSERT_TRUE(requeued.has_value());
        EXPECT_EQ(requeued->status, AiTaskStatus::kPending);
        ASSERT_TRUE(requeued->errorMsg.has_value());
        EXPECT_EQ(requeued->errorMsg.value(), "provider timeout");

        const AiTaskClaimOutcome again = Claim("worker-b");

        ASSERT_TRUE(again.task.has_value());
        EXPECT_EQ(again.task->id, taskId.value());
        EXPECT_EQ(again.task->retryCount, 2);
    }

    TEST_F(AiTaskDaoTest, FailsPermanentFailureImmediately)
    {
        const std::optional<std::uint64_t> taskId = CreateTask(false);

        ASSERT_TRUE(taskId.has_value());
        ASSERT_TRUE(Claim("worker-a").task.has_value());

        jobpilot::core::ConnectionPool::Transaction transaction =
            pool->BeginTransaction();

        const std::optional<AiTaskRecord> failed =
            dao->FailTask(
                transaction,
                taskId.value(),
                "worker-a",
                "invalid provider response",
                false,
                AiTaskLeasePolicy{});

        transaction.Commit();

        ASSERT_TRUE(failed.has_value());
        EXPECT_EQ(failed->status, AiTaskStatus::kFailed);
        EXPECT_EQ(failed->retryCount, 1);

        EXPECT_FALSE(Claim("worker-b").task.has_value());
    }

    TEST_F(AiTaskDaoTest, CompletionRequiresOwnerAndLiveLease)
    {
        const std::optional<std::uint64_t> taskId = CreateTask(false);

        ASSERT_TRUE(taskId.has_value());

        AiTaskLeasePolicy lease;
        lease.leaseSeconds = 1;

        ASSERT_TRUE(
            Claim("worker-a", lease)
                .task.has_value());

        jobpilot::core::ConnectionPool::Transaction wrongOwner =
            pool->BeginTransaction();

        EXPECT_FALSE(
            dao->CompleteTask(
                wrongOwner,
                taskId.value(),
                "worker-b",
                "{\"score\": 90}"));

        wrongOwner.Rollback();

        ExpireLease(taskId.value());

        jobpilot::core::ConnectionPool::Transaction expired =
            pool->BeginTransaction();

        EXPECT_FALSE(
            dao->CompleteTask(
                expired,
                taskId.value(),
                "worker-a",
                "{\"score\": 90}"));

        expired.Rollback();

        pool->Execute(
            "UPDATE ai_tasks "
            "SET lease_expire_at = TIMESTAMPADD(SECOND, 300, NOW()) "
            "WHERE id = ?",
            {taskId.value()});

        jobpilot::core::ConnectionPool::Transaction owner =
            pool->BeginTransaction();

        EXPECT_TRUE(
            dao->CompleteTask(
                owner,
                taskId.value(),
                "worker-a",
                "{\"score\": 90}"));

        owner.Commit();

        const std::optional<AiTaskRecord> completed =
            dao->FindOwnedTask(userId, taskId.value());

        ASSERT_TRUE(completed.has_value());
        EXPECT_EQ(completed->status, AiTaskStatus::kSuccess);
        ASSERT_TRUE(completed->result.has_value());
        EXPECT_EQ(completed->result.value(), "{\"score\": 90}");
        EXPECT_FALSE(completed->errorMsg.has_value());
    }

} // namespace jobpilot::service
