#include "jobpilot/service/user/user_dao.h"

#include <exception>
#include <memory>
#include <string>

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
                jobpilot::test::UniqueEmail(
                    "user-dao");
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

        std::unique_ptr<UserDao> dao;
        std::string email;
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
} // namespace jobpilot::service
