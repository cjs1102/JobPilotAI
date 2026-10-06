#include "jobpilot/service/user/user_service.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "bcrypt.h"
#include "jobpilot/cache/session_manager.h"
#include "jobpilot/service/user/user_dao.h"
#include "test_user_support.h"

namespace jobpilot::service
{
    namespace
    {
        bool IsLowerHex(
            const std::string &value)
        {
            if (value.empty())
            {
                return false;
            }

            for (char character : value)
            {
                const bool digit =
                    character >= '0' &&
                    character <= '9';

                const bool lower =
                    character >= 'a' &&
                    character <= 'f';

                if (!digit && !lower)
                {
                    return false;
                }
            }

            return true;
        }
    } // namespace

    class UserServiceTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            pool = jobpilot::test::MakeConnectionPool();
            redis = jobpilot::test::MakeRedisClient();

            sessions =
                std::make_unique<
                    jobpilot::core::SessionManager>(
                    *redis);

            dao = std::make_unique<UserDao>(*pool);

            service =
                std::make_unique<UserService>(
                    *dao,
                    *sessions);
        }

        void TearDown() override
        {
            for (const std::string &token : tokens)
            {
                redis->Del("session:" + token);
            }

            for (const std::string &email : emails)
            {
                jobpilot::test::CleanupUser(
                    pool.get(),
                    email);
            }
        }

        std::string TrackEmail(
            const std::string &prefix)
        {
            const std::string email =
                jobpilot::test::UniqueEmail(prefix);

            emails.push_back(email);
            return email;
        }

        std::unique_ptr<
            jobpilot::core::ConnectionPool>
            pool;

        std::unique_ptr<jobpilot::core::RedisClient> redis;

        std::unique_ptr<
            jobpilot::core::SessionManager>
            sessions;

        std::unique_ptr<UserDao> dao;
        std::unique_ptr<UserService> service;

        std::vector<std::string> emails;
        std::vector<std::string> tokens;
    };

    TEST_F(UserServiceTest, RegistersHashedUser)
    {
        const std::string email =
            TrackEmail("service-register");

        const std::string password =
            "correct horse battery staple";

        const ServiceResult<std::uint64_t> result =
            service->Register(
                "tom",
                email,
                password);

        ASSERT_EQ(result.code, ErrorCode::kSuccess);
        EXPECT_GT(result.value, 0U);

        const std::optional<UserRecord> user =
            dao->FindByEmail(email);

        ASSERT_TRUE(user.has_value());
        EXPECT_NE(user->passwordHash, password);
        EXPECT_EQ(
            bcrypt_checkpw(
                password.c_str(),
                user->passwordHash.c_str()),
            0);
    }

    TEST_F(UserServiceTest, RejectsDuplicateAndInvalidRegistration)
    {
        const std::string email =
            TrackEmail("service-duplicate");

        ASSERT_EQ(
            service->Register(
                   "first",
                   email,
                   "password-one")
                .code,
            ErrorCode::kSuccess);

        EXPECT_EQ(
            service->Register(
                   "second",
                   email,
                   "password-two")
                .code,
            ErrorCode::kEmailExists);

        EXPECT_EQ(
            service->Register(
                   "",
                   TrackEmail("service-invalid-name"),
                   "password")
                .code,
            ErrorCode::kInvalidParam);

        EXPECT_EQ(
            service->Register(
                   "tom",
                   "not-an-email",
                   "password")
                .code,
            ErrorCode::kInvalidParam);

        EXPECT_EQ(
            service->Register(
                   "tom",
                   TrackEmail("service-empty-password"),
                   "")
                .code,
            ErrorCode::kInvalidParam);
    }

    TEST_F(UserServiceTest, LogsInAndCreatesSession)
    {
        const std::string email =
            TrackEmail("service-login");

        const ServiceResult<std::uint64_t> registration =
            service->Register(
                "tom",
                email,
                "password");

        ASSERT_EQ(
            registration.code,
            ErrorCode::kSuccess);

        const ServiceResult<LoginData> login =
            service->Login(
                email,
                "password");

        ASSERT_EQ(login.code, ErrorCode::kSuccess);
        EXPECT_EQ(
            login.value.userId,
            registration.value);
        EXPECT_EQ(login.value.token.size(), 64U);
        EXPECT_TRUE(IsLowerHex(login.value.token));

        tokens.push_back(login.value.token);

        const std::optional<std::uint64_t> sessionUser =
            sessions->GetUserId(
                login.value.token);

        ASSERT_TRUE(sessionUser.has_value());
        EXPECT_EQ(
            sessionUser.value(),
            registration.value);

        EXPECT_EQ(
            service->Login(
                   email,
                   "wrong-password")
                .code,
            ErrorCode::kInvalidPassword);

        EXPECT_EQ(
            service->Login(
                   jobpilot::test::UniqueEmail(
                       "service-missing"),
                   "password")
                .code,
            ErrorCode::kUserNotFound);
    }

    TEST_F(UserServiceTest, ReturnsCurrentUserAndRejectsInvalidToken)
    {
        const std::string email =
            TrackEmail("service-me");

        ASSERT_EQ(
            service->Register(
                   "tom",
                   email,
                   "password")
                .code,
            ErrorCode::kSuccess);

        const ServiceResult<LoginData> login =
            service->Login(
                email,
                "password");

        ASSERT_EQ(login.code, ErrorCode::kSuccess);
        tokens.push_back(login.value.token);

        const ServiceResult<UserRecord> current =
            service->GetCurrentUser(
                login.value.token);

        ASSERT_EQ(current.code, ErrorCode::kSuccess);
        EXPECT_EQ(current.value.email, email);
        EXPECT_EQ(current.value.username, "tom");

        EXPECT_EQ(
            service->GetCurrentUser("").code,
            ErrorCode::kUnauthorized);

        EXPECT_EQ(
            service->GetCurrentUser(
                   "invalid-token")
                .code,
            ErrorCode::kUnauthorized);
    }

    TEST_F(UserServiceTest, RejectsExpiredSession)
    {
        const std::string email =
            TrackEmail("service-expiration");

        jobpilot::core::SessionManager shortSessions(
            *redis,
            1);

        UserService shortService(
            *dao,
            shortSessions);

        ASSERT_EQ(
            shortService.Register(
                       "tom",
                       email,
                       "password")
                .code,
            ErrorCode::kSuccess);

        const ServiceResult<LoginData> login =
            shortService.Login(
                email,
                "password");

        ASSERT_EQ(login.code, ErrorCode::kSuccess);
        tokens.push_back(login.value.token);

        std::this_thread::sleep_for(
            std::chrono::seconds(2));

        EXPECT_EQ(
            shortService.GetCurrentUser(
                            login.value.token)
                .code,
            ErrorCode::kUnauthorized);
    }

    TEST_F(UserServiceTest, SupportsConcurrentRegistrationAndSessions)
    {
        constexpr std::size_t kThreadCount = 4;

        std::vector<std::string> threadEmails;
        threadEmails.reserve(kThreadCount);

        for (std::size_t i = 0;
             i < kThreadCount;
             ++i)
        {
            threadEmails.push_back(
                TrackEmail("service-concurrent"));
        }

        std::atomic<bool> failed{false};
        std::vector<std::uint64_t> userIds(
            kThreadCount,
            0);
        std::vector<std::string> threadTokens(
            kThreadCount);

        std::vector<std::thread> threads;
        threads.reserve(kThreadCount);

        for (std::size_t i = 0;
             i < kThreadCount;
             ++i)
        {
            threads.emplace_back(
                [this,
                 &failed,
                 &userIds,
                 &threadTokens,
                 &threadEmails,
                 i]()
                {
                    const ServiceResult<std::uint64_t>
                        registration =
                            service->Register(
                                "user-" +
                                    std::to_string(i),
                                threadEmails[i],
                                "password");

                    if (registration.code !=
                        ErrorCode::kSuccess)
                    {
                        failed.store(true);
                        return;
                    }

                    const ServiceResult<LoginData> login =
                        service->Login(
                            threadEmails[i],
                            "password");

                    if (login.code !=
                        ErrorCode::kSuccess)
                    {
                        failed.store(true);
                        return;
                    }

                    userIds[i] =
                        registration.value;

                    threadTokens[i] =
                        login.value.token;
                });
        }

        for (std::thread &thread : threads)
        {
            thread.join();
        }

        EXPECT_FALSE(failed.load());

        for (const std::string &token : threadTokens)
        {
            ASSERT_FALSE(token.empty());
            tokens.push_back(token);
        }

        std::vector<std::thread> readers;
        readers.reserve(kThreadCount);

        for (std::size_t i = 0;
             i < kThreadCount;
             ++i)
        {
            readers.emplace_back(
                [this,
                 &failed,
                 &threadTokens,
                 &userIds,
                 i]()
                {
                    const ServiceResult<UserRecord> user =
                        service->GetCurrentUser(
                            threadTokens[i]);

                    if (user.code != ErrorCode::kSuccess ||
                        user.value.id != userIds[i])
                    {
                        failed.store(true);
                    }
                });
        }

        for (std::thread &thread : readers)
        {
            thread.join();
        }

        EXPECT_FALSE(failed.load());
    }
} // namespace jobpilot::service
