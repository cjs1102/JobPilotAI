#pragma once

#include <cstdint>
#include <string>

#include "jobpilot/service/result.h"

namespace jobpilot::core
{
    class SessionManager;
}

namespace jobpilot::service
{
    class ISessionAuthenticator
    {
    public:
        virtual ~ISessionAuthenticator() = default;

        virtual ServiceResult<std::uint64_t> Authenticate(
            const std::string &token) = 0;
    };

    class SessionAuthenticator final
        : public ISessionAuthenticator
    {
    public:
        explicit SessionAuthenticator(
            jobpilot::core::SessionManager &sessions);

        ServiceResult<std::uint64_t> Authenticate(
            const std::string &token) override;

    private:
        jobpilot::core::SessionManager &sessions_;
    };

} // namespace jobpilot::service
