#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "jobpilot/service/result.h"
#include "jobpilot/service/user/user_dao_interface.h"

namespace jobpilot::core
{
    class SessionManager;
}

namespace jobpilot::service
{

    struct LoginData
    {
        std::uint64_t userId = 0;
        std::string token;
    };

    class UserService
    {
    public:
        UserService(
            IUserDao &userDao,
            jobpilot::core::SessionManager &sessions);

        ServiceResult<std::uint64_t> Register(
            const std::string &username,
            const std::string &email,
            const std::string &password);

        ServiceResult<LoginData> Login(
            const std::string &email,
            const std::string &password);

        ServiceResult<UserRecord> GetCurrentUser(
            const std::string &token);

        ServiceResult<std::uint64_t> Authenticate(
            const std::string &token);

        ServiceResult<UserProfile> GetProfile(
            std::uint64_t userId);

        ServiceResult<UserProfile> UpdateProfile(
            std::uint64_t userId,
            const UserProfile &profile);

        ServiceResult<std::vector<UserSkill>> ListSkills(
            std::uint64_t userId);

        ServiceResult<UserSkill> UpsertSkill(
            std::uint64_t userId,
            const std::string &name,
            std::int32_t level);

        ServiceResult<bool> DeleteSkill(
            std::uint64_t userId,
            std::uint64_t skillId);

    private:
        static bool IsValidUsername(
            const std::string &username);

        static bool IsValidEmail(
            const std::string &email);

        static bool IsValidPassword(
            const std::string &password);

        static bool IsValidOptionalText(
            const std::optional<std::string> &value,
            std::size_t maxLength);

        static bool IsValidProfile(
            const UserProfile &profile);

        static bool IsValidSkill(
            const std::string &name,
            std::int32_t level);

        static std::string HashPassword(
            const std::string &password);

        static bool VerifyPassword(
            const std::string &password,
            const std::string &hash);

        static std::string GenerateToken();

    private:
        IUserDao &userDao_;
        jobpilot::core::SessionManager &sessions_;
    };

} // namespace jobpilot::service
