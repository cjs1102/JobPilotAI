// 0015 D3: the tailored-artifact store. Covers the ownership seams (base
// version through IResumeDao::OwnedVersionExists, application by user),
// the draft/confirmed boundary and the fact-gate guard, plus proof that
// resume_versions is never overwritten.

#include "jobpilot/service/resume/resume_artifact_dao.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "jobpilot/database/mysql_pool.h"
#include "jobpilot/service/resume/resume_dao.h"
#include "jobpilot/service/user/user_dao.h"
#include "test_job_support.h"
#include "test_user_support.h"

namespace jobpilot::service
{
    class ResumeArtifactDaoTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            pool = jobpilot::test::MakeConnectionPool();
            userDao = std::make_unique<UserDao>(*pool);
            resumeDao = std::make_unique<ResumeDao>(*pool);
            artifactDao =
                std::make_unique<ResumeArtifactDao>(
                    *pool,
                    *resumeDao);

            email = jobpilot::test::UniqueEmail("resume-artifact");
            otherEmail =
                jobpilot::test::UniqueEmail("resume-artifact-other");

            userId = userDao
                         ->CreateUser("artifact-user", email, "hash")
                         .value();
            otherUserId = userDao
                              ->CreateUser(
                                  "artifact-other",
                                  otherEmail,
                                  "hash")
                              .value();

            {
                auto transaction = pool->BeginTransaction();

                resumeId = resumeDao
                               ->CreateResume(
                                   transaction,
                                   userId,
                                   "C++ backend")
                               .value();

                EXPECT_TRUE(
                    resumeDao->LockOwnedResume(
                        transaction,
                        userId,
                        resumeId));

                versionId = resumeDao
                                ->CreateVersion(
                                    transaction,
                                    resumeId,
                                    1,
                                    "1/resumes/" +
                                        std::to_string(resumeId) +
                                        "/v1.txt",
                                    std::string("resume text v1"))
                                .value();

                transaction.Commit();
            }

            otherVersionId = 0;

            {
                auto transaction = pool->BeginTransaction();

                const std::uint64_t otherResumeId =
                    resumeDao
                        ->CreateResume(
                            transaction,
                            otherUserId,
                            "other resume")
                        .value();

                EXPECT_TRUE(
                    resumeDao->LockOwnedResume(
                        transaction,
                        otherUserId,
                        otherResumeId));

                otherVersionId = resumeDao
                                     ->CreateVersion(
                                         transaction,
                                         otherResumeId,
                                         1,
                                         "1/resumes/" +
                                             std::to_string(
                                                 otherResumeId) +
                                             "/v1.txt",
                                         std::string("other text"))
                                     .value();

                transaction.Commit();
            }

            jobId = jobpilot::test::InsertJob(
                pool.get(),
                "Artifact Co",
                "Artifact Job",
                "Remote",
                "description",
                jobpilot::test::UniqueJobToken("artifact-url"),
                1,
                "2026-10-01 10:00:00");

            const jobpilot::core::ExecutionResult application =
                pool->Execute(
                    "INSERT INTO applications "
                    "(user_id, job_id, resume_version_id, status) "
                    "VALUES (?, ?, ?, ?)",
                    {userId, jobId, versionId, std::int64_t(0)});

            ASSERT_EQ(application.affectedRows, 1U);
            applicationId = application.lastInsertId;

            const jobpilot::core::ExecutionResult otherApplication =
                pool->Execute(
                    "INSERT INTO applications "
                    "(user_id, job_id, resume_version_id, status) "
                    "VALUES (?, ?, ?, ?)",
                    {otherUserId,
                     jobId,
                     otherVersionId,
                     std::int64_t(0)});

            ASSERT_EQ(otherApplication.affectedRows, 1U);
            otherApplicationId = otherApplication.lastInsertId;
        }

        void TearDown() override
        {
            pool->Execute(
                "DELETE FROM resume_artifacts WHERE user_id IN (?, ?)",
                {userId, otherUserId});
            pool->Execute(
                "DELETE FROM applications WHERE id IN (?, ?)",
                {applicationId, otherApplicationId});
            jobpilot::test::CleanupJob(pool.get(), jobId);
            jobpilot::test::CleanupUser(pool.get(), email);
            jobpilot::test::CleanupUser(pool.get(), otherEmail);
        }

        ResumeArtifactWrite MakeWrite(
            std::int32_t factCheckStatus)
        {
            ResumeArtifactWrite write;
            write.userId = userId;
            write.jobId = jobId;
            write.applicationId = applicationId;
            write.baseVersionId = versionId;
            write.content = "草稿内容 draft body";
            write.generator = "rule_v1";
            write.sourceVersion = "tailor_rule_v1";
            write.factCheckStatus = factCheckStatus;
            write.factCheckReport = "{\"status\":\"pass\"}";
            return write;
        }

        std::unique_ptr<jobpilot::core::ConnectionPool> pool;
        std::unique_ptr<UserDao> userDao;
        std::unique_ptr<ResumeDao> resumeDao;
        std::unique_ptr<ResumeArtifactDao> artifactDao;

        std::string email;
        std::string otherEmail;
        std::uint64_t userId = 0;
        std::uint64_t otherUserId = 0;
        std::uint64_t resumeId = 0;
        std::uint64_t versionId = 0;
        std::uint64_t otherVersionId = 0;
        std::uint64_t jobId = 0;
        std::uint64_t applicationId = 0;
        std::uint64_t otherApplicationId = 0;
    };

    TEST_F(ResumeArtifactDaoTest, CreatesFindsAndListsAnOwnedArtifact)
    {
        std::uint64_t artifactId = 0;

        {
            auto transaction = pool->BeginTransaction();

            EXPECT_EQ(
                artifactDao->CheckOwnership(
                    transaction,
                    userId,
                    versionId,
                    applicationId),
                ArtifactOwnership::kOk);

            const std::optional<std::uint64_t> created =
                artifactDao->Create(
                    transaction,
                    MakeWrite(1));

            ASSERT_TRUE(created.has_value());
            artifactId = created.value();
            transaction.Commit();
        }

        const std::optional<ResumeArtifactRecord> stored =
            artifactDao->Find(userId, artifactId);

        ASSERT_TRUE(stored.has_value());
        EXPECT_EQ(stored->userId, userId);
        EXPECT_EQ(stored->jobId, jobId);
        EXPECT_EQ(stored->applicationId, applicationId);
        EXPECT_EQ(stored->baseVersionId, versionId);
        EXPECT_EQ(stored->artifactType, "resume");
        EXPECT_EQ(stored->language, "zh");
        EXPECT_EQ(stored->content, "草稿内容 draft body");
        EXPECT_TRUE(stored->contentBytes.has_value());
        EXPECT_EQ(
            stored->contentBytes.value(),
            stored->content.value().size());
        EXPECT_EQ(stored->generator, "rule_v1");
        EXPECT_EQ(stored->sourceVersion, "tailor_rule_v1");
        EXPECT_EQ(stored->factCheckStatus, 1);
        ASSERT_TRUE(stored->factCheckReport.has_value());
        // MySQL normalises JSON column text ({"status":"pass"} is returned as
        // {"status": "pass"}), so assert the values, not the byte layout.
        EXPECT_NE(
            stored->factCheckReport.value().find("status"),
            std::string::npos);
        EXPECT_NE(
            stored->factCheckReport.value().find("pass"),
            std::string::npos);

        // A fresh artifact is a DRAFT: confirmed_at is NULL.
        EXPECT_FALSE(stored->confirmedAt.has_value());

        const std::vector<ResumeArtifactRecord> listed =
            artifactDao->List(userId);

        ASSERT_EQ(listed.size(), 1U);
        EXPECT_EQ(listed.front().id, artifactId);

        // Another user can never read it through the ownership-scoped find.
        EXPECT_FALSE(
            artifactDao->Find(otherUserId, artifactId).has_value());
    }

    TEST_F(ResumeArtifactDaoTest, OwnershipRejectsForeignVersionAndApplication)
    {
        auto transaction = pool->BeginTransaction();

        // Another user's resume version is not a valid base.
        EXPECT_EQ(
            artifactDao->CheckOwnership(
                transaction,
                otherUserId,
                versionId,
                std::nullopt),
            ArtifactOwnership::kBaseVersionNotFound);

        // A missing version is not a valid base.
        EXPECT_EQ(
            artifactDao->CheckOwnership(
                transaction,
                userId,
                999999999ULL,
                std::nullopt),
            ArtifactOwnership::kBaseVersionNotFound);

        // Another user's application is not a valid anchor.
        EXPECT_EQ(
            artifactDao->CheckOwnership(
                transaction,
                userId,
                versionId,
                otherApplicationId),
            ArtifactOwnership::kApplicationNotFound);

        EXPECT_EQ(
            artifactDao->CheckOwnership(
                transaction,
                userId,
                versionId,
                applicationId),
            ArtifactOwnership::kOk);
    }

    TEST_F(ResumeArtifactDaoTest, ConfirmationIsFactGateGuardedAndNeverTouchesVersions)
    {
        const jobpilot::core::Result before =
            pool->Query(
                "SELECT content FROM resume_versions WHERE id = ?",
                {versionId});

        ASSERT_EQ(before.rowCount(), 1U);
        const std::string versionContent =
            before.value(0, 0).value();

        std::uint64_t failedId = 0;
        std::uint64_t passedId = 0;

        {
            auto transaction = pool->BeginTransaction();
            failedId = artifactDao
                           ->Create(transaction, MakeWrite(2))
                           .value();
            passedId = artifactDao
                           ->Create(transaction, MakeWrite(1))
                           .value();
            transaction.Commit();
        }

        {
            auto transaction = pool->BeginTransaction();

            // A failed fact check can never be confirmed.
            EXPECT_FALSE(
                artifactDao->Confirm(
                    transaction,
                    userId,
                    failedId));
            transaction.Commit();
        }

        const std::optional<ResumeArtifactRecord> failed =
            artifactDao->Find(userId, failedId);
        ASSERT_TRUE(failed.has_value());
        EXPECT_EQ(failed->factCheckStatus, 2);
        EXPECT_FALSE(failed->confirmedAt.has_value());

        {
            auto transaction = pool->BeginTransaction();

            EXPECT_TRUE(
                artifactDao->Confirm(
                    transaction,
                    userId,
                    passedId));
            transaction.Commit();
        }

        const std::optional<ResumeArtifactRecord> confirmed =
            artifactDao->Find(userId, passedId);
        ASSERT_TRUE(confirmed.has_value());
        EXPECT_TRUE(confirmed->confirmedAt.has_value());

        // Confirming twice is not a second state change.
        {
            auto transaction = pool->BeginTransaction();
            EXPECT_FALSE(
                artifactDao->Confirm(
                    transaction,
                    userId,
                    passedId));
            transaction.Commit();
        }

        // The base version was never overwritten.
        const jobpilot::core::Result after =
            pool->Query(
                "SELECT content FROM resume_versions WHERE id = ?",
                {versionId});

        ASSERT_EQ(after.rowCount(), 1U);
        EXPECT_EQ(after.value(0, 0).value(), versionContent);
        EXPECT_EQ(versionContent, "resume text v1");
    }

} // namespace jobpilot::service
