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
          sessions_(sessions),
          authenticator_(sessions)
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

    bool UserService::IsValidOptionalText(
        const std::optional<std::string> &value,
        std::size_t maxLength)
    {
        return !value.has_value() ||
               value->size() <= maxLength;
    }

    bool UserService::IsValidProfile(
        const UserProfile &profile)
    {
        if (profile.userId == 0 ||
            !IsValidOptionalText(
                profile.targetPosition,
                128) ||
            !IsValidOptionalText(
                profile.experience,
                64) ||
            !IsValidOptionalText(
                profile.location,
                64) ||
            !IsValidOptionalText(
                profile.summary,
                65535))
        {
            return false;
        }

        return !profile.salaryMin.has_value() ||
               !profile.salaryMax.has_value() ||
               profile.salaryMin.value() <=
                   profile.salaryMax.value();
    }

    bool UserService::IsValidSkill(
        const std::string &name,
        std::int32_t level)
    {
        return !name.empty() &&
               name.size() <= 64 &&
               level >= 1 &&
               level <= 3;
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
        const ServiceResult<std::uint64_t> authenticated =
            Authenticate(token);

        if (authenticated.code != ErrorCode::kSuccess)
        {
            return ServiceResult<UserRecord>{
                authenticated.code,
                {}};
        }

        try
        {
            const std::optional<UserRecord> user =
                userDao_.FindById(authenticated.value);

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

    ServiceResult<std::uint64_t> UserService::Authenticate(
        const std::string &token)
    {
        return authenticator_.Authenticate(token);
    }

    ServiceResult<UserProfile> UserService::GetProfile(
        std::uint64_t userId)
    {
        if (userId == 0)
        {
            return ServiceResult<UserProfile>{
                ErrorCode::kUnauthorized,
                {}};
        }

        try
        {
            const std::optional<UserProfile> profile =
                userDao_.FindProfile(userId);

            if (!profile.has_value())
            {
                UserProfile empty;
                empty.userId = userId;

                return ServiceResult<UserProfile>{
                    ErrorCode::kSuccess,
                    empty};
            }

            return ServiceResult<UserProfile>{
                ErrorCode::kSuccess,
                profile.value()};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "profile lookup failed: {}",
                error.what());

            return ServiceResult<UserProfile>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

    ServiceResult<UserProfile> UserService::UpdateProfile(
        std::uint64_t userId,
        const UserProfile &profile)
    {
        UserProfile updated = profile;
        updated.userId = userId;

        if (!IsValidProfile(updated))
        {
            return ServiceResult<UserProfile>{
                ErrorCode::kInvalidParam,
                {}};
        }

        try
        {
            if (!userDao_.UpsertProfile(updated))
            {
                return ServiceResult<UserProfile>{
                    ErrorCode::kDatabaseError,
                    {}};
            }

            const std::optional<UserProfile> stored =
                userDao_.FindProfile(userId);

            if (!stored.has_value())
            {
                return ServiceResult<UserProfile>{
                    ErrorCode::kDatabaseError,
                    {}};
            }

            return ServiceResult<UserProfile>{
                ErrorCode::kSuccess,
                stored.value()};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "profile update failed: {}",
                error.what());

            return ServiceResult<UserProfile>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

    ServiceResult<std::vector<UserSkill>>
    UserService::ListSkills(std::uint64_t userId)
    {
        if (userId == 0)
        {
            return ServiceResult<std::vector<UserSkill>>{
                ErrorCode::kUnauthorized,
                {}};
        }

        try
        {
            return ServiceResult<std::vector<UserSkill>>{
                ErrorCode::kSuccess,
                userDao_.ListSkills(userId)};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "skill list failed: {}",
                error.what());

            return ServiceResult<std::vector<UserSkill>>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

    ServiceResult<UserSkill> UserService::UpsertSkill(
        std::uint64_t userId,
        const std::string &name,
        std::int32_t level)
    {
        if (userId == 0)
        {
            return ServiceResult<UserSkill>{
                ErrorCode::kUnauthorized,
                {}};
        }

        if (!IsValidSkill(name, level))
        {
            return ServiceResult<UserSkill>{
                ErrorCode::kInvalidParam,
                {}};
        }

        try
        {
            const std::optional<UserSkill> skill =
                userDao_.UpsertSkill(
                    userId,
                    name,
                    level);

            if (!skill.has_value())
            {
                return ServiceResult<UserSkill>{
                    ErrorCode::kDatabaseError,
                    {}};
            }

            return ServiceResult<UserSkill>{
                ErrorCode::kSuccess,
                skill.value()};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "skill upsert failed: {}",
                error.what());

            return ServiceResult<UserSkill>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

    ServiceResult<bool> UserService::DeleteSkill(
        std::uint64_t userId,
        std::uint64_t skillId)
    {
        if (userId == 0)
        {
            return ServiceResult<bool>{
                ErrorCode::kUnauthorized,
                false};
        }

        if (skillId == 0)
        {
            return ServiceResult<bool>{
                ErrorCode::kInvalidParam,
                false};
        }

        try
        {
            return ServiceResult<bool>{
                ErrorCode::kSuccess,
                userDao_.DeleteSkill(
                    userId,
                    skillId)};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "skill delete failed: {}",
                error.what());

            return ServiceResult<bool>{
                ErrorCode::kDatabaseError,
                false};
        }
    }

} // namespace jobpilot::service
