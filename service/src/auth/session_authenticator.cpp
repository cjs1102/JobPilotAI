#include "jobpilot/service/auth/session_authenticator.h"

#include <cctype>
#include <exception>
#include <optional>

#include "jobpilot/cache/session_manager.h"
#include "jobpilot/core/logger.h"

namespace jobpilot::service
{
    namespace
    {
        std::string Trim(
            const std::string &value)
        {
            std::size_t begin = 0;
            std::size_t end = value.size();

            while (begin < end &&
                   std::isspace(
                       static_cast<unsigned char>(
                           value[begin])))
            {
                ++begin;
            }

            while (end > begin &&
                   std::isspace(
                       static_cast<unsigned char>(
                           value[end - 1])))
            {
                --end;
            }

            return value.substr(
                begin,
                end - begin);
        }

        std::string ToLower(
            std::string value)
        {
            for (char &character : value)
            {
                character = static_cast<char>(
                    std::tolower(
                        static_cast<unsigned char>(
                            character)));
            }

            return value;
        }
    } // namespace

    std::optional<std::string> ParseBearerToken(
        const std::string &authorization)
    {
        constexpr const char *kPrefix =
            "Bearer ";

        if (authorization.size() <= 7 ||
            ToLower(authorization.substr(0, 7)) !=
                ToLower(kPrefix))
        {
            return std::nullopt;
        }

        const std::string token =
            Trim(authorization.substr(7));

        if (token.empty())
        {
            return std::nullopt;
        }

        return token;
    }

    SessionAuthenticator::SessionAuthenticator(
        jobpilot::core::SessionManager &sessions)
        : sessions_(sessions)
    {
    }

    ServiceResult<std::uint64_t>
    SessionAuthenticator::Authenticate(
        const std::string &token)
    {
        if (token.empty())
        {
            return ServiceResult<std::uint64_t>{
                ErrorCode::kUnauthorized,
                0};
        }

        try
        {
            const std::optional<std::uint64_t> userId =
                sessions_.GetUserId(token);

            if (!userId.has_value())
            {
                return ServiceResult<std::uint64_t>{
                    ErrorCode::kUnauthorized,
                    0};
            }

            return ServiceResult<std::uint64_t>{
                ErrorCode::kSuccess,
                userId.value()};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "session authentication failed: {}",
                error.what());

            return ServiceResult<std::uint64_t>{
                ErrorCode::kInternalError,
                0};
        }
    }

} // namespace jobpilot::service
