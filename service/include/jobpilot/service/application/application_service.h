#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "jobpilot/service/application/application_dao_interface.h"
#include "jobpilot/service/auth/session_authenticator.h"
#include "jobpilot/service/result.h"

namespace jobpilot::core
{
    class ConnectionPool;
}

namespace jobpilot::service
{
    class IJobDao;
    class IResumeDao;

    struct ApplicationCreateData
    {
        std::uint64_t applicationId = 0;
        std::int32_t status = 0;
    };

    struct ApplicationPage
    {
        std::vector<ApplicationListItem> list;
        std::size_t total = 0;
        std::size_t page = 1;
        std::size_t pageSize = 20;
    };

    class ApplicationService
    {
    public:
        ApplicationService(
            IApplicationDao &applicationDao,
            IJobDao &jobDao,
            IResumeDao &resumeDao,
            jobpilot::core::ConnectionPool &pool,
            ISessionAuthenticator &authenticator);

        ServiceResult<std::uint64_t> Authenticate(
            const std::string &token);

        ServiceResult<ApplicationCreateData> Create(
            std::uint64_t userId,
            std::uint64_t jobId,
            std::uint64_t resumeVersionId);

        ServiceResult<ApplicationPage> List(
            std::uint64_t userId,
            const std::optional<std::int32_t> &status,
            std::size_t page,
            std::size_t pageSize);

        ServiceResult<std::int32_t> UpdateStatus(
            std::uint64_t userId,
            std::uint64_t applicationId,
            std::int32_t targetStatus);

        ServiceResult<ApplicationStatistics> Statistics(
            std::uint64_t userId);

    private:
        static bool IsValidStatus(
            std::int32_t status);

        static bool IsValidTransition(
            std::int32_t currentStatus,
            std::int32_t targetStatus);

        IApplicationDao &applicationDao_;
        IJobDao &jobDao_;
        IResumeDao &resumeDao_;
        jobpilot::core::ConnectionPool &pool_;
        ISessionAuthenticator &authenticator_;
    };

} // namespace jobpilot::service
