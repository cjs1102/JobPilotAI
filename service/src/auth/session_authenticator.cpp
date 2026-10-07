#include "jobpilot/service/auth/session_authenticator.h"

#include <exception>
#include <optional>

#include "jobpilot/cache/session_manager.h"
#include "jobpilot/core/logger.h"

namespace jobpilot::service
{
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
