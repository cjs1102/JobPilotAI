#include "jobpilot/service/auth/session_authenticator.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "jobpilot/cache/session_manager.h"
#include "test_user_support.h"

namespace jobpilot::service
{
    class SessionAuthenticatorTest
        : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            redis = jobpilot::test::MakeRedisClient();
            sessions =
                std::make_unique<
                    jobpilot::core::SessionManager>(
                    *redis);
            authenticator =
                std::make_unique<SessionAuthenticator>(
                    *sessions);
        }

        void TearDown() override
        {
            for (const std::string &token : tokens)
            {
                redis->Del("session:" + token);
            }
        }

        std::unique_ptr<jobpilot::core::RedisClient> redis;
        std::unique_ptr<jobpilot::core::SessionManager>
            sessions;
        std::unique_ptr<SessionAuthenticator>
            authenticator;
        std::vector<std::string> tokens;
    };

    TEST_F(SessionAuthenticatorTest, ResolvesSessionUserId)
    {
        const std::string token =
            "session-authenticator-valid";

        tokens.push_back(token);
        sessions->Set(
            token,
            42U);

        const ServiceResult<std::uint64_t> result =
            authenticator->Authenticate(token);

        ASSERT_EQ(result.code, ErrorCode::kSuccess);
        EXPECT_EQ(result.value, 42U);
    }

    TEST_F(SessionAuthenticatorTest, RejectsMissingSession)
    {
        const ServiceResult<std::uint64_t> empty =
            authenticator->Authenticate("");

        EXPECT_EQ(empty.code, ErrorCode::kUnauthorized);

        const ServiceResult<std::uint64_t> missing =
            authenticator->Authenticate(
                "session-authenticator-missing");

        EXPECT_EQ(
            missing.code,
            ErrorCode::kUnauthorized);
    }

    TEST_F(SessionAuthenticatorTest, ParsesBearerHeader)
    {
        const std::optional<std::string> token =
            ParseBearerToken(
                "Bearer abc123");

        ASSERT_TRUE(token.has_value());
        EXPECT_EQ(token.value(), "abc123");

        EXPECT_FALSE(
            ParseBearerToken("Basic abc123")
                .has_value());
        EXPECT_FALSE(
            ParseBearerToken("Bearer ")
                .has_value());
    }
} // namespace jobpilot::service
