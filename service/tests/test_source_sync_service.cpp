#include "jobpilot/service/job/source_sync_service.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "jobpilot/database/mysql_pool.h"
#include "jobpilot/service/job/source_sync_dao.h"
#include "test_job_support.h"
#include "test_user_support.h"

namespace jobpilot::service
{
    namespace
    {
        class RecordingSourceSyncDao final
            : public ISourceSyncDao
        {
        public:
            explicit RecordingSourceSyncDao(
                SourceSyncDao *delegate = nullptr)
                : delegate_(delegate)
            {
            }

            SourceObservationApplyResult ApplyObservation(
                jobpilot::core::ConnectionPool::Transaction &transaction,
                const SourceObservation &observation) override
            {
                transactions.push_back(&transaction);
                ++applyCalls;
                observations.push_back(observation);

                if (throwOnApply)
                {
                    throw std::runtime_error(
                        "forced apply failure");
                }

                if (delegate_ != nullptr)
                {
                    return delegate_->ApplyObservation(
                        transaction,
                        observation);
                }

                return applyResult;
            }

            SourceObservationCloseResult CloseUnseenForScope(
                jobpilot::core::ConnectionPool::Transaction &transaction,
                const SourceObservation &observation,
                const MissingPolicy &policy) override
            {
                transactions.push_back(&transaction);
                ++closeCalls;
                observations.push_back(observation);
                policies.push_back(policy);

                if (throwOnClose)
                {
                    throw std::runtime_error(
                        "forced close failure");
                }

                if (delegate_ != nullptr)
                {
                    return delegate_->CloseUnseenForScope(
                        transaction,
                        observation,
                        policy);
                }

                return closeResult;
            }

            std::vector<
                jobpilot::core::ConnectionPool::Transaction *>
                transactions;
            std::vector<SourceObservation> observations;
            std::vector<MissingPolicy> policies;
            std::size_t applyCalls = 0;
            std::size_t closeCalls = 0;
            bool throwOnApply = false;
            bool throwOnClose = false;
            SourceObservationApplyResult applyResult{3, 2, 5, 0, {}};
            SourceObservationCloseResult closeResult{4, 1};

        private:
            SourceSyncDao *delegate_;
        };
    } // namespace

    class SourceSyncServiceTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            pool = jobpilot::test::MakeConnectionPool();
            scope = jobpilot::test::UniqueJobToken(
                "sync-service");
        }

        void TearDown() override
        {
            const jobpilot::core::Result jobs =
                pool->Query(
                    "SELECT id FROM jobs WHERE source_scope = ?",
                    {scope});

            for (std::size_t row = 0;
                 row < jobs.rowCount();
                 ++row)
            {
                const jobpilot::core::Result::Value &value =
                    jobs.value(row, 0);

                if (value.has_value())
                {
                    jobpilot::test::CleanupJob(
                        pool.get(),
                        static_cast<std::uint64_t>(
                            std::stoull(value.value())));
                }
            }

            for (std::uint64_t jobId : extraJobIds)
            {
                jobpilot::test::CleanupJob(pool.get(), jobId);
            }
        }

        std::string Url(const std::string &name) const
        {
            return "https://example.invalid/" + scope + "/" + name;
        }

        SourceObservationJob MakeObservedJob(
            const std::string &name) const
        {
            SourceObservationJob item;
            item.job.company = "Greenhouse Company";
            item.job.title = "Backend " + name;
            item.job.location = "Remote";
            item.job.salaryMin = 20;
            item.job.salaryMax = 40;
            item.job.salaryCurrency = "CNY";
            item.job.description = "Observed " + name;
            item.job.source = "greenhouse";
            item.job.sourceUrl = Url(name);
            item.externalId = "ext-" + name;
            item.publishedAt = "2026-09-20 08:00:00";
            item.requirements = {{"C++", 3}, {"Linux", 2}};
            return item;
        }

        SourceObservation MakeObservation(
            const std::string &observedAt = "2026-10-01 00:00:00",
            bool complete = true) const
        {
            SourceObservation observation;
            observation.source = "greenhouse";
            observation.sourceScope = scope;
            observation.complete = complete;
            observation.observedAt = observedAt;
            observation.jobs.push_back(MakeObservedJob("one"));
            return observation;
        }

        std::unique_ptr<jobpilot::core::ConnectionPool> pool;
        std::string scope;
        std::vector<std::uint64_t> extraJobIds;
    };

    TEST_F(SourceSyncServiceTest, RejectsInvalidObservationsWithoutCallingDao)
    {
        RecordingSourceSyncDao dao;
        SourceSyncService service(dao, *pool);

        SourceObservation unsupportedSource = MakeObservation();
        unsupportedSource.source = "manual";
        EXPECT_EQ(
            service.Sync(unsupportedSource).code,
            ErrorCode::kInvalidParam);

        SourceObservation emptyScope = MakeObservation();
        emptyScope.sourceScope.clear();
        EXPECT_EQ(
            service.Sync(emptyScope).code,
            ErrorCode::kInvalidParam);

        SourceObservation badTimestamp = MakeObservation();
        badTimestamp.observedAt = "2026-10-01T00:00:00";
        EXPECT_EQ(
            service.Sync(badTimestamp).code,
            ErrorCode::kInvalidParam);

        SourceObservation emptyIncomplete = MakeObservation(
            "2026-10-01 00:00:00",
            false);
        emptyIncomplete.jobs.clear();
        EXPECT_EQ(
            service.Sync(emptyIncomplete).code,
            ErrorCode::kInvalidParam);

        SourceObservation oversized = MakeObservation();
        oversized.jobs.clear();

        for (std::size_t index = 0;
             index <= kMaxObservationBatchSize;
             ++index)
        {
            oversized.jobs.push_back(
                MakeObservedJob(
                    "bulk-" + std::to_string(index)));
        }

        EXPECT_EQ(
            service.Sync(oversized).code,
            ErrorCode::kInvalidParam);

        EXPECT_EQ(dao.applyCalls, 0U);
        EXPECT_EQ(dao.closeCalls, 0U);
    }

    TEST_F(SourceSyncServiceTest, PropagatesCountersAndCompleteness)
    {
        RecordingSourceSyncDao dao;
        dao.applyResult = {3, 2, 5, 0, {}};
        dao.closeResult = {7, 4};
        SourceSyncService service(dao, *pool);

        const SourceObservation observation = MakeObservation();
        const ServiceResult<SourceSyncResult> result =
            service.Sync(observation);

        ASSERT_EQ(result.code, ErrorCode::kSuccess);
        EXPECT_EQ(result.value.created, 3U);
        EXPECT_EQ(result.value.updated, 2U);
        EXPECT_EQ(result.value.seen, 5U);
        EXPECT_EQ(result.value.closed, 4U);
        EXPECT_TRUE(result.value.complete);

        EXPECT_EQ(dao.applyCalls, 1U);
        EXPECT_EQ(dao.closeCalls, 1U);

        ASSERT_EQ(dao.policies.size(), 1U);
        EXPECT_EQ(
            dao.policies[0].closeAfterMisses,
            kDefaultCloseAfterMisses);
        EXPECT_EQ(
            dao.policies[0].graceSeconds,
            kDefaultGraceSeconds);
    }

    TEST_F(SourceSyncServiceTest, IncompleteObservationNeverCloses)
    {
        RecordingSourceSyncDao dao;
        dao.applyResult = {0, 1, 1, 0, {}};
        dao.closeResult = {9, 9};
        SourceSyncService service(dao, *pool);

        const ServiceResult<SourceSyncResult> result =
            service.Sync(
                MakeObservation(
                    "2026-10-01 00:00:00",
                    false));

        ASSERT_EQ(result.code, ErrorCode::kSuccess);
        EXPECT_EQ(result.value.created, 0U);
        EXPECT_EQ(result.value.updated, 1U);
        EXPECT_EQ(result.value.seen, 1U);
        EXPECT_EQ(result.value.closed, 0U);
        EXPECT_FALSE(result.value.complete);

        EXPECT_EQ(dao.applyCalls, 1U);
        EXPECT_EQ(dao.closeCalls, 0U);
    }

    TEST_F(SourceSyncServiceTest, EmptyCompleteBoardStillRunsClosePass)
    {
        RecordingSourceSyncDao dao;
        dao.applyResult = {0, 0, 0, 0, {}};
        dao.closeResult = {2, 0};
        SourceSyncService service(dao, *pool);

        SourceObservation observation = MakeObservation();
        observation.jobs.clear();

        const ServiceResult<SourceSyncResult> result =
            service.Sync(observation);

        ASSERT_EQ(result.code, ErrorCode::kSuccess);
        EXPECT_EQ(result.value.seen, 0U);
        EXPECT_EQ(result.value.created, 0U);
        EXPECT_EQ(result.value.updated, 0U);
        EXPECT_EQ(result.value.closed, 0U);
        EXPECT_TRUE(result.value.complete);

        EXPECT_EQ(dao.applyCalls, 1U);
        EXPECT_EQ(dao.closeCalls, 1U);
    }

    TEST_F(SourceSyncServiceTest, PreservesObservationIdentity)
    {
        RecordingSourceSyncDao dao;
        SourceSyncService service(dao, *pool);

        SourceObservation observation = MakeObservation();
        observation.jobs.push_back(MakeObservedJob("two"));

        ASSERT_EQ(
            service.Sync(observation).code,
            ErrorCode::kSuccess);
        ASSERT_EQ(dao.observations.size(), 2U);

        const SourceObservation &captured =
            dao.observations.front();

        EXPECT_EQ(captured.source, "greenhouse");
        EXPECT_EQ(captured.sourceScope, scope);
        EXPECT_TRUE(captured.complete);
        EXPECT_EQ(captured.observedAt, "2026-10-01 00:00:00");
        ASSERT_EQ(captured.jobs.size(), 2U);

        EXPECT_EQ(
            captured.jobs[0].job.sourceUrl,
            Url("one"));
        EXPECT_EQ(
            captured.jobs[1].job.sourceUrl,
            Url("two"));
        EXPECT_EQ(captured.jobs[0].job.source, "greenhouse");
        EXPECT_EQ(captured.jobs[1].job.source, "greenhouse");
        ASSERT_TRUE(captured.jobs[0].externalId.has_value());
        EXPECT_EQ(captured.jobs[0].externalId.value(), "ext-one");
        ASSERT_TRUE(captured.jobs[1].externalId.has_value());
        EXPECT_EQ(captured.jobs[1].externalId.value(), "ext-two");
        ASSERT_TRUE(captured.jobs[1].publishedAt.has_value());
        EXPECT_EQ(
            captured.jobs[1].publishedAt.value(),
            "2026-09-20 08:00:00");
        ASSERT_EQ(captured.jobs[1].requirements.size(), 2U);
        EXPECT_EQ(captured.jobs[1].requirements[0].skill, "C++");

        EXPECT_EQ(
            dao.observations.back().source,
            captured.source);
    }

    TEST_F(SourceSyncServiceTest, UsesOneTransactionAndRollsBackOnFailure)
    {
        SourceSyncDao delegate;
        RecordingSourceSyncDao dao(&delegate);
        dao.throwOnClose = true;
        SourceSyncService service(dao, *pool);

        const ServiceResult<SourceSyncResult> result =
            service.Sync(MakeObservation());

        EXPECT_EQ(result.code, ErrorCode::kDatabaseError);
        ASSERT_GE(dao.transactions.size(), 2U);

        for (const auto *transaction : dao.transactions)
        {
            EXPECT_EQ(
                transaction,
                dao.transactions.front());
        }

        const jobpilot::core::Result count =
            pool->Query(
                "SELECT COUNT(*) FROM jobs WHERE source_url = ?",
                {Url("one")});

        ASSERT_EQ(count.rowCount(), 1U);
        ASSERT_TRUE(count.value(0, 0).has_value());
        EXPECT_EQ(count.value(0, 0).value(), "0");
    }

    // 0015A constraint: a single rejected item writes NOTHING (no transaction
    // is opened) and the batch still honours F14 manual-record priority, both
    // when the rejected item sits next to a valid one.
    TEST_F(SourceSyncServiceTest, RejectsWriteNothingAndPreserveManualPriority)
    {
        SourceSyncDao delegate;
        RecordingSourceSyncDao dao(&delegate);
        SourceSyncService service(dao, *pool);

        const std::string manualUrl = Url("manual-priority");
        const std::uint64_t manualId =
            jobpilot::test::InsertJob(
                pool.get(),
                "Manual Co",
                "Manual Job",
                "Remote",
                "Manual description",
                manualUrl,
                1,
                "2026-10-01 10:00:00");
        extraJobIds.push_back(manualId);

        // One rejected item (company_missing) + one valid item that collides
        // with the manual row.
        SourceObservation observation = MakeObservation();
        observation.jobs.clear();

        SourceObservationJob invalid = MakeObservedJob("invalid");
        invalid.job.company = "   ";
        observation.jobs.push_back(invalid);

        SourceObservationJob colliding = MakeObservedJob("collide");
        colliding.job.sourceUrl = manualUrl;
        observation.jobs.push_back(colliding);

        const ServiceResult<SourceSyncResult> rejected =
            service.Sync(observation);

        EXPECT_EQ(rejected.code, ErrorCode::kInvalidParam);
        EXPECT_EQ(rejected.value.quality.rejected, 1U);
        EXPECT_EQ(
            rejected.value.quality.diagnosticCode,
            "company_missing");
        EXPECT_EQ(
            rejected.value.quality.diagnosticField,
            "company");

        // No transaction was opened and nothing was written.
        EXPECT_EQ(dao.applyCalls, 0U);
        EXPECT_EQ(dao.closeCalls, 0U);

        const jobpilot::core::Result manual = pool->Query(
            "SELECT company, title, source, status, last_seen_at, "
            "source_scope FROM jobs WHERE id = ?",
            {manualId});

        ASSERT_EQ(manual.rowCount(), 1U);
        EXPECT_EQ(
            jobpilot::test::RequireValue(manual.value(0, 0)),
            "Manual Co");
        EXPECT_EQ(
            jobpilot::test::RequireValue(manual.value(0, 1)),
            "Manual Job");
        EXPECT_EQ(
            jobpilot::test::RequireValue(manual.value(0, 2)),
            "manual");
        EXPECT_EQ(
            jobpilot::test::RequireValue(manual.value(0, 3)),
            "1");
        EXPECT_FALSE(manual.value(0, 4).has_value());
        EXPECT_FALSE(manual.value(0, 5).has_value());

        const jobpilot::core::Result invalidRow = pool->Query(
            "SELECT COUNT(*) FROM jobs WHERE source_url = ?",
            {Url("invalid")});

        ASSERT_EQ(invalidRow.rowCount(), 1U);
        EXPECT_EQ(
            jobpilot::test::RequireValue(invalidRow.value(0, 0)),
            "0");

        // Without the rejected item the observation still skips the manual
        // collision (F14) and leaves the manual row untouched, including its
        // lifecycle fields.
        SourceObservation clean = MakeObservation();
        clean.jobs.clear();
        clean.jobs.push_back(colliding);

        const ServiceResult<SourceSyncResult> applied =
            service.Sync(clean);

        ASSERT_EQ(applied.code, ErrorCode::kSuccess);
        EXPECT_EQ(applied.value.seen, 1U);
        EXPECT_EQ(applied.value.skippedManual, 1U);
        EXPECT_EQ(applied.value.created, 0U);
        EXPECT_EQ(applied.value.updated, 0U);
        EXPECT_EQ(dao.applyCalls, 1U);

        const jobpilot::core::Result after = pool->Query(
            "SELECT company, title, source, status, last_seen_at, "
            "source_scope FROM jobs WHERE id = ?",
            {manualId});

        ASSERT_EQ(after.rowCount(), 1U);
        EXPECT_EQ(
            jobpilot::test::RequireValue(after.value(0, 0)),
            "Manual Co");
        EXPECT_EQ(
            jobpilot::test::RequireValue(after.value(0, 1)),
            "Manual Job");
        EXPECT_EQ(
            jobpilot::test::RequireValue(after.value(0, 2)),
            "manual");
        EXPECT_FALSE(after.value(0, 4).has_value());
        EXPECT_FALSE(after.value(0, 5).has_value());
    }

} // namespace jobpilot::service
