#include "jobpilot/service/resume/resume_dao.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "jobpilot/database/mysql_pool.h"
#include "jobpilot/service/user/user_dao.h"
#include "test_user_support.h"

namespace jobpilot::service
{
    class ResumeDaoTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            pool = jobpilot::test::MakeConnectionPool();
            userDao =
                std::make_unique<UserDao>(*pool);
            resumeDao =
                std::make_unique<ResumeDao>(*pool);
            email =
                jobpilot::test::UniqueEmail(
                    "resume-dao");

            const std::optional<std::uint64_t> userId =
                userDao->CreateUser(
                    "resume-dao-user",
                    email,
                    "hash");

            ASSERT_TRUE(userId.has_value());
            this->userId = userId.value();
        }

        void TearDown() override
        {
            jobpilot::test::CleanupUser(
                pool.get(),
                email);
        }

        std::unique_ptr<
            jobpilot::core::ConnectionPool>
            pool;
        std::unique_ptr<UserDao> userDao;
        std::unique_ptr<ResumeDao> resumeDao;
        std::uint64_t userId = 0;
        std::string email;
    };

    TEST_F(ResumeDaoTest, CreatesListsAndVersionsOwnedResume)
    {
        std::uint64_t resumeId = 0;
        std::uint64_t versionId = 0;

        {
            auto transaction =
                pool->BeginTransaction();

            const std::optional<std::uint64_t> created =
                resumeDao->CreateResume(
                    transaction,
                    userId,
                    "C++ backend");

            ASSERT_TRUE(created.has_value());
            resumeId = created.value();

            ASSERT_TRUE(
                resumeDao->LockOwnedResume(
                    transaction,
                    userId,
                    resumeId));

            const std::int32_t version =
                resumeDao->NextVersion(
                    transaction,
                    resumeId);

            EXPECT_EQ(version, 1);

            const std::optional<std::uint64_t> versionCreated =
                resumeDao->CreateVersion(
                    transaction,
                    resumeId,
                    version,
                    "1/resumes/" +
                        std::to_string(resumeId) +
                        "/v1.txt",
                    std::string("resume text"));

            ASSERT_TRUE(versionCreated.has_value());
            versionId = versionCreated.value();
            transaction.Commit();
        }

        const std::vector<ResumeRecord> resumes =
            resumeDao->ListResumes(
                userId,
                20,
                0);

        ASSERT_EQ(resumes.size(), 1U);
        EXPECT_EQ(resumes.front().id, resumeId);
        EXPECT_EQ(resumes.front().title, "C++ backend");
        EXPECT_EQ(resumes.front().latestVersion, 1);
        EXPECT_EQ(
            resumeDao->CountResumes(userId),
            1U);

        const std::optional<ResumeRecord> owned =
            resumeDao->FindOwnedResume(
                userId,
                resumeId);

        ASSERT_TRUE(owned.has_value());
        EXPECT_EQ(owned->latestVersion, 1);

        const std::vector<ResumeVersionRecord> versions =
            resumeDao->ListVersions(
                userId,
                resumeId);

        ASSERT_EQ(versions.size(), 1U);
        EXPECT_EQ(versions.front().id, versionId);
        EXPECT_EQ(versions.front().version, 1);
        ASSERT_TRUE(
            versions.front().content.has_value());
        EXPECT_EQ(
            versions.front().content.value(),
            "resume text");
    }

    TEST_F(ResumeDaoTest, ListsOnlyOwnedResumeVersions)
    {
        const std::string secondEmail =
            jobpilot::test::UniqueEmail(
                "resume-dao-second");

        const std::optional<std::uint64_t> secondUserId =
            userDao->CreateUser(
                "resume-dao-second",
                secondEmail,
                "hash");

        ASSERT_TRUE(secondUserId.has_value());

        std::uint64_t resumeId = 0;

        {
            auto transaction =
                pool->BeginTransaction();

            const std::optional<std::uint64_t> created =
                resumeDao->CreateResume(
                    transaction,
                    userId,
                    "Private");

            ASSERT_TRUE(created.has_value());
            resumeId = created.value();

            ASSERT_TRUE(
                resumeDao->LockOwnedResume(
                    transaction,
                    userId,
                    resumeId));

            ASSERT_TRUE(
                resumeDao->CreateVersion(
                        transaction,
                        resumeId,
                        1,
                        std::nullopt,
                        std::string("private"))
                    .has_value());

            transaction.Commit();
        }

        EXPECT_TRUE(
            resumeDao
                ->ListVersions(
                    secondUserId.value(),
                    resumeId)
                .empty());

        EXPECT_FALSE(
            resumeDao
                ->FindOwnedResume(
                    secondUserId.value(),
                    resumeId)
                .has_value());

        jobpilot::test::CleanupUser(
            pool.get(),
            secondEmail);
    }
} // namespace jobpilot::service
