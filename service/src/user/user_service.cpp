#include "jobpilot/service/user/user_service.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <system_error>

#include <sys/random.h>

#include "jobpilot/cache/session_manager.h"
#include "jobpilot/core/logger.h"
#include "bcrypt.h"

namespace jobpilot::service
{
    namespace
    {
        constexpr int kBcryptWorkFactor = 12;
        constexpr std::size_t kTokenBytes = 32;

        ServiceResult<std::uint64_t> ErrorResult(
            ErrorCode code)
        {
            return ServiceResult<std::uint64_t>{code, 0};
        }
    } // namespace

    UserService::UserService(
        IUserDao &userDao,
        jobpilot::core::SessionManager &sessions)
        : userDao_(userDao),
          sessions_(sessions)
    {
    }

    bool UserService::IsValidUsername(
        const std::string &username)
    {
        return !username.empty() &&
               username.size() <= 64;
    }

    bool UserService::IsValidEmail(
        const std::string &email)
    {
        if (email.empty() ||
            email.size() > 128 ||
            email.find('@') == std::string::npos)
        {
            return false;
        }

        for (char character : email)
        {
            if (character == ' ' ||
                character == '\t' ||
                character == '\r' ||
                character == '\n')
            {
                return false;
            }
        }

        return true;
    }

    bool UserService::IsValidPassword(
        const std::string &password)
    {
        return !password.empty() &&
               password.size() <= 128;
    }

    std::string UserService::HashPassword(
        const std::string &password)
    {
        std::array<char, BCRYPT_HASHSIZE> salt{};
        std::array<char, BCRYPT_HASHSIZE> hash{};

        if (bcrypt_gensalt(
                kBcryptWorkFactor,
                salt.data()) != 0)
        {
            throw std::runtime_error(
                "bcrypt_gensalt failed");
        }

        if (bcrypt_hashpw(
                password.c_str(),
                salt.data(),
                hash.data()) != 0)
        {
            throw std::runtime_error(
                "bcrypt_hashpw failed");
        }

        return std::string(hash.data());
    }

    bool UserService::VerifyPassword(
        const std::string &password,
        const std::string &hash)
    {
        const int result =
            bcrypt_checkpw(
                password.c_str(),
                hash.c_str());

        if (result < 0)
        {
            throw std::runtime_error(
                "bcrypt_checkpw failed");
        }

        return result == 0;
    }

    std::string UserService::GenerateToken()
    {
        std::array<unsigned char, kTokenBytes> bytes{};

        std::size_t offset = 0;

        while (offset < bytes.size())
        {
            const ssize_t received =
                ::getrandom(
                    bytes.data() + offset,
                    bytes.size() - offset,
                    0);

            if (received < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }

                throw std::system_error(
                    errno,
                    std::generic_category(),
                    "getrandom failed");
            }

            if (received == 0)
            {
                throw std::runtime_error(
                    "getrandom returned no data");
            }

            offset +=
                static_cast<std::size_t>(received);
        }

        constexpr char kHex[] =
            "0123456789abcdef";

        std::string token;
        token.reserve(bytes.size() * 2);

        for (unsigned char byte : bytes)
        {
            token.push_back(kHex[byte >> 4]);
            token.push_back(kHex[byte & 0x0F]);
        }

        return token;
    }

    ServiceResult<std::uint64_t> UserService::Register(
        const std::string &username,
        const std::string &email,
        const std::string &password)
    {
        if (!IsValidUsername(username) ||
            !IsValidEmail(email) ||
            !IsValidPassword(password))
        {
            return ErrorResult(
                ErrorCode::kInvalidParam);
        }

        try
        {
            if (userDao_.FindByEmail(email).has_value())
            {
                return ErrorResult(
                    ErrorCode::kEmailExists);
            }
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "user registration lookup failed: {}",
                error.what());

            return ErrorResult(
                ErrorCode::kDatabaseError);
        }

        std::string passwordHash;

        try
        {
            passwordHash =
                HashPassword(password);
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "password hashing failed: {}",
                error.what());

            return ErrorResult(
                ErrorCode::kInternalError);
        }

        try
        {
            const std::optional<std::uint64_t> userId =
                userDao_.CreateUser(
                    username,
                    email,
                    passwordHash);

            if (!userId.has_value())
            {
                return ErrorResult(
                    ErrorCode::kDatabaseError);
            }

            return ServiceResult<std::uint64_t>{
                ErrorCode::kSuccess,
                userId.value()};
        }
        catch (const std::exception &error)
        {
            try
            {
                if (userDao_.FindByEmail(email).has_value())
                {
                    return ErrorResult(
                        ErrorCode::kEmailExists);
                }
            }
            catch (const std::exception &)
            {
            }

            jobpilot::core::Logger::Get()->error(
                "user registration failed: {}",
                error.what());

            return ErrorResult(
                ErrorCode::kDatabaseError);
        }
    }

    ServiceResult<LoginData> UserService::Login(
        const std::string &email,
        const std::string &password)
    {
        if (!IsValidEmail(email) ||
            !IsValidPassword(password))
        {
            return ServiceResult<LoginData>{
                ErrorCode::kInvalidParam,
                {}};
        }

        try
        {
            const std::optional<UserRecord> user =
                userDao_.FindByEmail(email);

            if (!user.has_value())
            {
                return ServiceResult<LoginData>{
                    ErrorCode::kUserNotFound,
                    {}};
            }

            if (user->status != 1)
            {
                return ServiceResult<LoginData>{
                    ErrorCode::kForbidden,
                    {}};
            }

            if (!VerifyPassword(
                    password,
                    user->passwordHash))
            {
                return ServiceResult<LoginData>{
                    ErrorCode::kInvalidPassword,
                    {}};
            }

            const std::string token =
                GenerateToken();

            sessions_.Set(token, user->id);

            return ServiceResult<LoginData>{
                ErrorCode::kSuccess,
                LoginData{user->id, token}};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "user login failed: {}",
                error.what());

            return ServiceResult<LoginData>{
                ErrorCode::kInternalError,
                {}};
        }
    }

    ServiceResult<UserRecord> UserService::GetCurrentUser(
        const std::string &token)
    {
        if (token.empty())
        {
            return ServiceResult<UserRecord>{
                ErrorCode::kUnauthorized,
                {}};
        }

        try
        {
            const std::optional<std::uint64_t> userId =
                sessions_.GetUserId(token);

            if (!userId.has_value())
            {
                return ServiceResult<UserRecord>{
                    ErrorCode::kUnauthorized,
                    {}};
            }

            const std::optional<UserRecord> user =
                userDao_.FindById(userId.value());

            if (!user.has_value())
            {
                return ServiceResult<UserRecord>{
                    ErrorCode::kUserNotFound,
                    {}};
            }

            if (user->status != 1)
            {
                return ServiceResult<UserRecord>{
                    ErrorCode::kForbidden,
                    {}};
            }

            return ServiceResult<UserRecord>{
                ErrorCode::kSuccess,
                user.value()};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "current user lookup failed: {}",
                error.what());

            return ServiceResult<UserRecord>{
                ErrorCode::kInternalError,
                {}};
        }
    }

} // namespace jobpilot::service
