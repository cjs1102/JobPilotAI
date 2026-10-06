#pragma once

#include <cstdint>
#include <string>

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

    private:
        static bool IsValidUsername(
            const std::string &username);

        static bool IsValidEmail(
            const std::string &email);

        static bool IsValidPassword(
            const std::string &password);

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
