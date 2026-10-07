#include "jobpilot/service/job/source_sync_dao.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "jobpilot/database/mysql_pool.h"
#include "test_job_support.h"
#include "test_user_support.h"

namespace jobpilot::service
{
    class SourceSyncDaoTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            pool = jobpilot::test::MakeConnectionPool();
            dao = std::make_unique<SourceSyncDao>();
            scopeA = jobpilot::test::UniqueJobToken("sync-scope-a");
            scopeB = jobpilot::test::UniqueJobToken("sync-scope-b");
        }

        void TearDown() override
        {
            RemoveScopeJobs(scopeA);
            RemoveScopeJobs(scopeB);
        }

        static std::string Url(
            const std::string &scope,
            const std::string &name)
        {
            return "https://example.invalid/" + scope + "/" + name;
        }

        void RemoveScopeJobs(
            const std::string &scope)
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
        }

        static SourceObservation MakeObservation(
            const std::string &scope,
            const std::string &observedAt,
            bool complete = true)
        {
            SourceObservation observation;
            observation.source = "greenhouse";
            observation.sourceScope = scope;
            observation.complete = complete;
            observation.observedAt = observedAt;
            return observation;
        }

        static SourceObservationJob MakeObservedJob(
            const std::string &scope,
            const std::string &name,
            const std::optional<std::string> &publishedAt =
                std::nullopt)
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
            item.job.sourceUrl = Url(scope, name);
            item.externalId = "ext-" + name;
            item.publishedAt = publishedAt;
            item.requirements = {{"C++", 3}, {"Linux", 2}};
            return item;
        }

        SourceObservationApplyResult Apply(
            const SourceObservation &observation)
        {
            auto transaction = pool->BeginTransaction();
            const SourceObservationApplyResult result =
                dao->ApplyObservation(
                    transaction,
                    observation);
            transaction.Commit();
            return result;
        }

        SourceObservationCloseResult CloseUnseen(
            const SourceObservation &observation)
        {
            auto transaction = pool->BeginTransaction();
            const SourceObservationCloseResult result =
                dao->CloseUnseenForScope(
                    transaction,
                    observation,
                    MissingPolicy{});
            transaction.Commit();
            return result;
        }

        jobpilot::core::Result ReadJob(
            const std::string &scope,
            const std::string &name)
        {
            return pool->Query(
                "SELECT id, status, source_status, missing_count, "
                "closed_at, last_seen_at, last_verified_at, source_scope, "
                "external_id, published_at "
                "FROM jobs WHERE source_url = ?",
                {Url(scope, name)});
        }

        static std::string Text(
            const jobpilot::core::Result &result,
            std::size_t row,
            std::size_t column)
        {
            return jobpilot::test::RequireValue(
                result.value(row, column));
        }

        static bool IsNull(
            const jobpilot::core::Result &result,
            std::size_t row,
            std::size_t column)
        {
            return !result.value(row, column).has_value();
        }

        std::unique_ptr<jobpilot::core::ConnectionPool> pool;
        std::unique_ptr<SourceSyncDao> dao;
        std::string scopeA;
        std::string scopeB;
    };

    TEST_F(SourceSyncDaoTest, FirstObservationCreatesJobsWithFreshnessFields)
    {
        SourceObservation observation =
            MakeObservation(scopeA, "2026-10-01 00:00:00");
        observation.jobs.push_back(
            MakeObservedJob(scopeA, "one", "2026-09-20 08:00:00"));
        observation.jobs.push_back(
            MakeObservedJob(scopeA, "two"));

        const SourceObservationApplyResult applied =
            Apply(observation);

        EXPECT_EQ(applied.created, 2U);
        EXPECT_EQ(applied.updated, 0U);
        EXPECT_EQ(applied.seen, 2U);

        const jobpilot::core::Result one = ReadJob(scopeA, "one");

        ASSERT_EQ(one.rowCount(), 1U);
        EXPECT_EQ(Text(one, 0, 1), "1");
        EXPECT_EQ(Text(one, 0, 2), "ok");
        EXPECT_EQ(Text(one, 0, 3), "0");
        EXPECT_TRUE(IsNull(one, 0, 4));
        EXPECT_EQ(Text(one, 0, 5), "2026-10-01 00:00:00");
        EXPECT_EQ(Text(one, 0, 6), "2026-10-01 00:00:00");
        EXPECT_EQ(Text(one, 0, 7), scopeA);
        EXPECT_EQ(Text(one, 0, 8), "ext-one");
        EXPECT_EQ(Text(one, 0, 9), "2026-09-20 08:00:00");

        const jobpilot::core::Result two = ReadJob(scopeA, "two");

        ASSERT_EQ(two.rowCount(), 1U);
        EXPECT_EQ(Text(two, 0, 2), "ok");
        EXPECT_TRUE(IsNull(two, 0, 9));

        const jobpilot::core::Result requirements =
            pool->Query(
                "SELECT skill FROM job_requirements "
                "WHERE job_id = ? ORDER BY skill",
                {static_cast<std::uint64_t>(
                    std::stoull(Text(one, 0, 0)))});

        ASSERT_EQ(requirements.rowCount(), 2U);
        EXPECT_EQ(Text(requirements, 0, 0), "C++");
        EXPECT_EQ(Text(requirements, 1, 0), "Linux");
    }

    TEST_F(SourceSyncDaoTest, RepeatedObservationUpdatesWithoutDuplicating)
    {
        SourceObservation first =
            MakeObservation(scopeA, "2026-10-01 00:00:00");
        first.jobs.push_back(MakeObservedJob(scopeA, "one"));

        const SourceObservationApplyResult created = Apply(first);

        ASSERT_EQ(created.created, 1U);

        SourceObservation second =
            MakeObservation(scopeA, "2026-10-02 00:00:00");
        second.jobs.push_back(MakeObservedJob(scopeA, "one"));
        second.jobs[0].job.title = "Backend one v2";

        const SourceObservationApplyResult updated = Apply(second);

        EXPECT_EQ(updated.created, 0U);
        EXPECT_EQ(updated.updated, 1U);
        EXPECT_EQ(updated.seen, 1U);

        const jobpilot::core::Result count =
            pool->Query(
                "SELECT COUNT(*) FROM jobs WHERE source_url = ?",
                {Url(scopeA, "one")});

        EXPECT_EQ(Text(count, 0, 0), "1");

        const jobpilot::core::Result one = ReadJob(scopeA, "one");

        ASSERT_EQ(one.rowCount(), 1U);
        EXPECT_EQ(Text(one, 0, 2), "ok");
        EXPECT_EQ(Text(one, 0, 3), "0");
        EXPECT_EQ(Text(one, 0, 5), "2026-10-02 00:00:00");
    }

    TEST_F(SourceSyncDaoTest, CompleteAbsenceMarksMissingWithoutClosing)
    {
        SourceObservation present =
            MakeObservation(scopeA, "2026-10-01 00:00:00");
        present.jobs.push_back(MakeObservedJob(scopeA, "one"));
        Apply(present);

        SourceObservation absent =
            MakeObservation(scopeA, "2026-10-01 01:00:00");

        const SourceObservationCloseResult result =
            CloseUnseen(absent);

        EXPECT_EQ(result.missing, 1U);
        EXPECT_EQ(result.closed, 0U);

        const jobpilot::core::Result one = ReadJob(scopeA, "one");

        ASSERT_EQ(one.rowCount(), 1U);
        EXPECT_EQ(Text(one, 0, 1), "1");
        EXPECT_EQ(Text(one, 0, 2), "missing");
        EXPECT_EQ(Text(one, 0, 3), "1");
        EXPECT_TRUE(IsNull(one, 0, 4));
        EXPECT_EQ(Text(one, 0, 5), "2026-10-01 00:00:00");
    }

    TEST_F(SourceSyncDaoTest, SecondCompleteAbsenceCloses)
    {
        SourceObservation present =
            MakeObservation(scopeA, "2026-10-01 00:00:00");
        present.jobs.push_back(MakeObservedJob(scopeA, "one"));
        Apply(present);

        const SourceObservationCloseResult firstMiss =
            CloseUnseen(
                MakeObservation(scopeA, "2026-10-01 01:00:00"));

        ASSERT_EQ(firstMiss.missing, 1U);
        ASSERT_EQ(firstMiss.closed, 0U);

        const SourceObservationCloseResult secondMiss =
            CloseUnseen(
                MakeObservation(scopeA, "2026-10-01 02:00:00"));

        EXPECT_EQ(secondMiss.missing, 1U);
        EXPECT_EQ(secondMiss.closed, 1U);

        const jobpilot::core::Result one = ReadJob(scopeA, "one");

        ASSERT_EQ(one.rowCount(), 1U);
        EXPECT_EQ(Text(one, 0, 1), "0");
        EXPECT_EQ(Text(one, 0, 2), "closed");
        EXPECT_EQ(Text(one, 0, 3), "2");
        EXPECT_EQ(Text(one, 0, 4), "2026-10-01 02:00:00");
    }

    TEST_F(SourceSyncDaoTest, GraceWindowClosesOnFirstAbsence)
    {
        SourceObservation present =
            MakeObservation(scopeA, "2026-10-01 00:00:00");
        present.jobs.push_back(MakeObservedJob(scopeA, "one"));
        Apply(present);

        const SourceObservationCloseResult result =
            CloseUnseen(
                MakeObservation(scopeA, "2026-10-03 01:00:00"));

        EXPECT_EQ(result.missing, 1U);
        EXPECT_EQ(result.closed, 1U);

        const jobpilot::core::Result one = ReadJob(scopeA, "one");

        ASSERT_EQ(one.rowCount(), 1U);
        EXPECT_EQ(Text(one, 0, 1), "0");
        EXPECT_EQ(Text(one, 0, 2), "closed");
        EXPECT_EQ(Text(one, 0, 3), "1");
        EXPECT_EQ(Text(one, 0, 4), "2026-10-03 01:00:00");
    }

    TEST_F(SourceSyncDaoTest, GraceBoundaryAtFortyEightHoursDoesNotClose)
    {
        SourceObservation present =
            MakeObservation(scopeA, "2026-10-01 00:00:00");
        present.jobs.push_back(MakeObservedJob(scopeA, "one"));
        Apply(present);

        const SourceObservationCloseResult result =
            CloseUnseen(
                MakeObservation(scopeA, "2026-10-03 00:00:00"));

        EXPECT_EQ(result.missing, 1U);
        EXPECT_EQ(result.closed, 0U);

        const jobpilot::core::Result one = ReadJob(scopeA, "one");

        ASSERT_EQ(one.rowCount(), 1U);
        EXPECT_EQ(Text(one, 0, 1), "1");
        EXPECT_EQ(Text(one, 0, 2), "missing");
        EXPECT_EQ(Text(one, 0, 3), "1");
        EXPECT_TRUE(IsNull(one, 0, 4));
    }

    TEST_F(SourceSyncDaoTest, IncompleteObservationIsIgnored)
    {
        SourceObservation present =
            MakeObservation(scopeA, "2026-10-01 00:00:00");
        present.jobs.push_back(MakeObservedJob(scopeA, "one"));
        Apply(present);

        const SourceObservationCloseResult result =
            CloseUnseen(
                MakeObservation(
                    scopeA,
                    "2026-10-04 00:00:00",
                    false));

        EXPECT_EQ(result.missing, 0U);
        EXPECT_EQ(result.closed, 0U);

        const jobpilot::core::Result one = ReadJob(scopeA, "one");

        ASSERT_EQ(one.rowCount(), 1U);
        EXPECT_EQ(Text(one, 0, 1), "1");
        EXPECT_EQ(Text(one, 0, 2), "ok");
        EXPECT_EQ(Text(one, 0, 3), "0");
        EXPECT_TRUE(IsNull(one, 0, 4));
    }

    TEST_F(SourceSyncDaoTest, IncompleteObservationDoesNotAdvanceMissingCount)
    {
        SourceObservation present =
            MakeObservation(scopeA, "2026-10-01 00:00:00");
        present.jobs.push_back(MakeObservedJob(scopeA, "one"));
        Apply(present);

        const SourceObservationCloseResult firstMiss =
            CloseUnseen(
                MakeObservation(scopeA, "2026-10-01 01:00:00"));

        ASSERT_EQ(firstMiss.missing, 1U);

        const SourceObservationCloseResult incomplete =
            CloseUnseen(
                MakeObservation(
                    scopeA,
                    "2026-10-01 02:00:00",
                    false));

        EXPECT_EQ(incomplete.missing, 0U);
        EXPECT_EQ(incomplete.closed, 0U);

        const jobpilot::core::Result one = ReadJob(scopeA, "one");

        ASSERT_EQ(one.rowCount(), 1U);
        EXPECT_EQ(Text(one, 0, 1), "1");
        EXPECT_EQ(Text(one, 0, 2), "missing");
        EXPECT_EQ(Text(one, 0, 3), "1");
    }

    TEST_F(SourceSyncDaoTest, ReappearanceReopensAndResetsCounters)
    {
        SourceObservation present =
            MakeObservation(scopeA, "2026-10-01 00:00:00");
        present.jobs.push_back(MakeObservedJob(scopeA, "one"));
        Apply(present);

        ASSERT_EQ(
            CloseUnseen(
                MakeObservation(scopeA, "2026-10-01 01:00:00"))
                .closed,
            0U);

        const SourceObservationCloseResult closed =
            CloseUnseen(
                MakeObservation(scopeA, "2026-10-01 02:00:00"));

        ASSERT_EQ(closed.closed, 1U);

        SourceObservation reappeared =
            MakeObservation(scopeA, "2026-10-01 03:00:00");
        reappeared.jobs.push_back(MakeObservedJob(scopeA, "one"));

        const SourceObservationApplyResult applied =
            Apply(reappeared);

        EXPECT_EQ(applied.updated, 1U);

        const jobpilot::core::Result one = ReadJob(scopeA, "one");

        ASSERT_EQ(one.rowCount(), 1U);
        EXPECT_EQ(Text(one, 0, 1), "1");
        EXPECT_EQ(Text(one, 0, 2), "ok");
        EXPECT_EQ(Text(one, 0, 3), "0");
        EXPECT_TRUE(IsNull(one, 0, 4));
        EXPECT_EQ(Text(one, 0, 5), "2026-10-01 03:00:00");
        EXPECT_EQ(Text(one, 0, 6), "2026-10-01 03:00:00");
    }

    TEST_F(SourceSyncDaoTest, ScopeIsolationPreventsClosingOtherBoards)
    {
        SourceObservation boardA =
            MakeObservation(scopeA, "2026-10-01 00:00:00");
        boardA.jobs.push_back(MakeObservedJob(scopeA, "one"));
        Apply(boardA);

        SourceObservation boardB =
            MakeObservation(scopeB, "2026-10-01 00:00:00");
        boardB.jobs.push_back(MakeObservedJob(scopeB, "two"));
        Apply(boardB);

        const SourceObservationCloseResult firstMiss =
            CloseUnseen(
                MakeObservation(scopeA, "2026-10-01 01:00:00"));

        EXPECT_EQ(firstMiss.missing, 1U);
        EXPECT_EQ(firstMiss.closed, 0U);

        const SourceObservationCloseResult secondMiss =
            CloseUnseen(
                MakeObservation(scopeA, "2026-10-01 02:00:00"));

        EXPECT_EQ(secondMiss.missing, 1U);
        EXPECT_EQ(secondMiss.closed, 1U);

        const jobpilot::core::Result one = ReadJob(scopeA, "one");

        ASSERT_EQ(one.rowCount(), 1U);
        EXPECT_EQ(Text(one, 0, 1), "0");
        EXPECT_EQ(Text(one, 0, 2), "closed");

        const jobpilot::core::Result two = ReadJob(scopeB, "two");

        ASSERT_EQ(two.rowCount(), 1U);
        EXPECT_EQ(Text(two, 0, 1), "1");
        EXPECT_EQ(Text(two, 0, 2), "ok");
        EXPECT_EQ(Text(two, 0, 3), "0");
        EXPECT_TRUE(IsNull(two, 0, 4));
    }

    TEST_F(SourceSyncDaoTest, DoesNotTouchOtherSourcesOrManualJobs)
    {
        const std::string manualUrl =
            "https://example.invalid/manual/" + scopeA;

        const std::uint64_t manualJobId =
            jobpilot::test::InsertJob(
                pool.get(),
                "Manual Company",
                "Manual Job",
                std::nullopt,
                std::nullopt,
                manualUrl,
                1,
                "");

        const SourceObservationCloseResult result =
            CloseUnseen(
                MakeObservation(scopeA, "2026-10-01 00:00:00"));

        EXPECT_EQ(result.missing, 0U);
        EXPECT_EQ(result.closed, 0U);

        const jobpilot::core::Result manual =
            pool->Query(
                "SELECT status, missing_count, source_status, closed_at "
                "FROM jobs WHERE id = ?",
                {manualJobId});

        ASSERT_EQ(manual.rowCount(), 1U);
        EXPECT_EQ(Text(manual, 0, 0), "1");
        EXPECT_EQ(Text(manual, 0, 1), "0");
        EXPECT_EQ(Text(manual, 0, 2), "unknown");
        EXPECT_TRUE(IsNull(manual, 0, 3));

        jobpilot::test::CleanupJob(pool.get(), manualJobId);
    }

    TEST_F(SourceSyncDaoTest, RollbackDiscardsObservation)
    {
        SourceObservation observation =
            MakeObservation(scopeA, "2026-10-01 00:00:00");
        observation.jobs.push_back(
            MakeObservedJob(scopeA, "rollback"));

        {
            auto transaction = pool->BeginTransaction();
            dao->ApplyObservation(transaction, observation);
            transaction.Rollback();
        }

        EXPECT_EQ(
            ReadJob(scopeA, "rollback").rowCount(),
            0U);
    }

} // namespace jobpilot::service
