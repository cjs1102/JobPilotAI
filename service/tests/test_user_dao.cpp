#include "jobpilot/service/user/user_dao.h"

#include <exception>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "test_user_support.h"

namespace jobpilot::service
{
    class UserDaoTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            pool = jobpilot::test::MakeConnectionPool();
            dao = std::make_unique<UserDao>(*pool);
            email =
                TrackEmail("user-dao");
        }

        void TearDown() override
        {
            for (const std::string &trackedEmail : emails)
            {
                jobpilot::test::CleanupUser(
                    pool.get(),
                    trackedEmail);
            }
        }

        std::string TrackEmail(
            const std::string &prefix)
        {
            const std::string trackedEmail =
                jobpilot::test::UniqueEmail(prefix);

            emails.push_back(trackedEmail);
            return trackedEmail;
        }

        std::unique_ptr<
            jobpilot::core::ConnectionPool>
            pool;

        std::unique_ptr<UserDao> dao;
        std::string email;
        std::vector<std::string> emails;
    };

    TEST_F(UserDaoTest, CreatesAndFindsUser)
    {
        const std::string username =
            "name' OR 1=1 --";

        const std::optional<std::uint64_t> userId =
            dao->CreateUser(
                username,
                email,
                "bcrypt-hash");

        ASSERT_TRUE(userId.has_value());
        EXPECT_GT(userId.value(), 0U);

        const std::optional<UserRecord> byEmail =
            dao->FindByEmail(email);

        ASSERT_TRUE(byEmail.has_value());
        EXPECT_EQ(byEmail->id, userId.value());
        EXPECT_EQ(byEmail->username, username);
        EXPECT_EQ(byEmail->email, email);
        EXPECT_EQ(byEmail->passwordHash, "bcrypt-hash");
        EXPECT_TRUE(byEmail->avatar.empty());
        EXPECT_EQ(byEmail->status, 1);

        const std::optional<UserRecord> byId =
            dao->FindById(userId.value());

        ASSERT_TRUE(byId.has_value());
        EXPECT_EQ(byId->email, email);
    }

    TEST_F(UserDaoTest, RejectsDuplicateEmail)
    {
        ASSERT_TRUE(
            dao->CreateUser(
                   "first",
                   email,
                   "hash-one")
                .has_value());

        EXPECT_THROW(
            dao->CreateUser(
                "second",
                email,
                "hash-two"),
            std::exception);
    }

    TEST_F(UserDaoTest, ReturnsNulloptForMissingUser)
    {
        EXPECT_FALSE(
            dao->FindByEmail(email).has_value());

        EXPECT_FALSE(
            dao->FindById(999999999ULL).has_value());
    }

    TEST_F(UserDaoTest, UpsertsAndReadsProfile)
    {
        const std::optional<std::uint64_t> userId =
            dao->CreateUser(
                "profile-user",
                email,
                "hash");

        ASSERT_TRUE(userId.has_value());

        EXPECT_FALSE(
            dao->FindProfile(userId.value()).has_value());

        UserProfile profile;
        profile.userId = userId.value();
        profile.targetPosition = "C++后端开发";
        profile.experience = "应届生";
        profile.location = "深圳";
        profile.salaryMin = 15;
        profile.salaryMax = 25;
        profile.summary = "熟悉 Linux 网络编程";
        profile.educationLevel = 3;
        profile.graduationYear = 2026;
        profile.campusStatus = 2;

        ASSERT_TRUE(dao->UpsertProfile(profile));

        const std::optional<UserProfile> stored =
            dao->FindProfile(userId.value());

        ASSERT_TRUE(stored.has_value());
        EXPECT_EQ(stored->targetPosition, "C++后端开发");
        EXPECT_EQ(stored->salaryMin, 15U);
        EXPECT_EQ(stored->salaryMax, 25U);
        // 0015 A2: the three nullable candidate columns round-trip.
        EXPECT_EQ(stored->educationLevel, 3U);
        EXPECT_EQ(stored->graduationYear, 2026U);
        EXPECT_EQ(stored->campusStatus, 2U);

        profile.location = "上海";
        profile.salaryMin.reset();
        profile.salaryMax.reset();
        // NULL = unknown: an omitted key must stay NULL, never a default class.
        profile.educationLevel.reset();
        profile.graduationYear.reset();
        profile.campusStatus.reset();

        ASSERT_TRUE(dao->UpsertProfile(profile));

        const std::optional<UserProfile> updated =
            dao->FindProfile(userId.value());

        ASSERT_TRUE(updated.has_value());
        EXPECT_EQ(updated->location, "上海");
        EXPECT_FALSE(updated->salaryMin.has_value());
        EXPECT_FALSE(updated->salaryMax.has_value());
        EXPECT_FALSE(updated->educationLevel.has_value());
        EXPECT_FALSE(updated->graduationYear.has_value());
        EXPECT_FALSE(updated->campusStatus.has_value());
    }

    TEST_F(UserDaoTest, UpsertsListsAndDeletesScopedSkills)
    {
        const std::optional<std::uint64_t> firstUserId =
            dao->CreateUser(
                "skill-first",
                email,
                "hash");

        const std::string secondEmail =
            TrackEmail("user-dao-second");

        const std::optional<std::uint64_t> secondUserId =
            dao->CreateUser(
                "skill-second",
                secondEmail,
                "hash");

        ASSERT_TRUE(firstUserId.has_value());
        ASSERT_TRUE(secondUserId.has_value());

        const std::optional<UserSkill> created =
            dao->UpsertSkill(
                firstUserId.value(),
                "C++",
                1);

        ASSERT_TRUE(created.has_value());

        const std::optional<UserSkill> updated =
            dao->UpsertSkill(
                firstUserId.value(),
                "C++",
                3);

        ASSERT_TRUE(updated.has_value());
        EXPECT_EQ(updated->id, created->id);
        EXPECT_EQ(updated->level, 3);

        ASSERT_TRUE(
            dao->UpsertSkill(
                    secondUserId.value(),
                    "Redis",
                    2)
                .has_value());

        const std::vector<UserSkill> firstSkills =
            dao->ListSkills(firstUserId.value());

        ASSERT_EQ(firstSkills.size(), 1U);
        EXPECT_EQ(firstSkills.front().name, "C++");
        EXPECT_EQ(firstSkills.front().level, 3);

        EXPECT_TRUE(
            dao->DeleteSkill(
                secondUserId.value(),
                created->id));

        EXPECT_EQ(
            dao->ListSkills(firstUserId.value()).size(),
            1U);

        EXPECT_TRUE(
            dao->DeleteSkill(
                firstUserId.value(),
                created->id));

        EXPECT_TRUE(
            dao->ListSkills(firstUserId.value()).empty());
    }
} // namespace jobpilot::service
