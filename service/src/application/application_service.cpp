#include "jobpilot/service/application/application_service.h"

#include <exception>
#include <limits>
#include <optional>
#include <utility>

#include "jobpilot/core/logger.h"
#include "jobpilot/database/mysql_pool.h"
#include "jobpilot/service/job/job_dao_interface.h"
#include "jobpilot/service/resume/resume_dao_interface.h"

namespace jobpilot::service
{
    namespace
    {
        constexpr std::int32_t kFavorite = 0;
        constexpr std::int32_t kPending = 1;
        constexpr std::int32_t kApplied = 2;
        constexpr std::int32_t kOffer = 6;
        constexpr std::int32_t kRejected = 7;
        constexpr std::size_t kMaxPageSize = 100;
    } // namespace

    ApplicationService::ApplicationService(
        IApplicationDao &applicationDao,
        IJobDao &jobDao,
        IResumeDao &resumeDao,
        jobpilot::core::ConnectionPool &pool,
        ISessionAuthenticator &authenticator)
        : applicationDao_(applicationDao),
          jobDao_(jobDao),
          resumeDao_(resumeDao),
          pool_(pool),
          authenticator_(authenticator)
    {
    }

    ServiceResult<std::uint64_t> ApplicationService::Authenticate(
        const std::string &token)
    {
        return authenticator_.Authenticate(token);
    }

    bool ApplicationService::IsValidStatus(
        std::int32_t status)
    {
        return status >= kFavorite &&
               status <= kRejected;
    }

    bool ApplicationService::IsValidTransition(
        std::int32_t currentStatus,
        std::int32_t targetStatus)
    {
        if (!IsValidStatus(currentStatus) ||
            !IsValidStatus(targetStatus))
        {
            return false;
        }

        if (targetStatus == kRejected)
        {
            return currentStatus >= kFavorite &&
                   currentStatus < kOffer;
        }

        if (currentStatus >= kOffer)
        {
            return false;
        }

        if (targetStatus == currentStatus + 1)
        {
            return true;
        }

        return false;
    }

    ServiceResult<ApplicationCreateData>
    ApplicationService::Create(
        std::uint64_t userId,
        std::uint64_t jobId,
        std::uint64_t resumeVersionId)
    {
        if (userId == 0)
        {
            return ServiceResult<ApplicationCreateData>{
                ErrorCode::kUnauthorized,
                {}};
        }

        if (jobId == 0 || resumeVersionId == 0)
        {
            return ServiceResult<ApplicationCreateData>{
                ErrorCode::kInvalidParam,
                {}};
        }

        try
        {
            auto transaction =
                pool_.BeginTransaction();

            if (!jobDao_.ActiveJobExists(
                    transaction,
                    jobId))
            {
                return ServiceResult<ApplicationCreateData>{
                    ErrorCode::kJobNotFound,
                    {}};
            }

            if (!resumeDao_.OwnedVersionExists(
                    transaction,
                    userId,
                    resumeVersionId))
            {
                return ServiceResult<ApplicationCreateData>{
                    ErrorCode::kResumeNotFound,
                    {}};
            }

            const ApplicationCreateResult created =
                applicationDao_.Create(
                    transaction,
                    userId,
                    jobId,
                    resumeVersionId);

            if (created.status ==
                ApplicationCreateStatus::kDuplicate)
            {
                return ServiceResult<ApplicationCreateData>{
                    ErrorCode::kDuplicateApplication,
                    {}};
            }

            transaction.Commit();

            return ServiceResult<ApplicationCreateData>{
                ErrorCode::kSuccess,
                ApplicationCreateData{
                    created.applicationId,
                    kFavorite}};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "application creation failed: {}",
                error.what());

            return ServiceResult<ApplicationCreateData>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

    ServiceResult<ApplicationPage> ApplicationService::List(
        std::uint64_t userId,
        const std::optional<std::int32_t> &status,
        std::size_t page,
        std::size_t pageSize)
    {
        if (userId == 0)
        {
            return ServiceResult<ApplicationPage>{
                ErrorCode::kUnauthorized,
                {}};
        }

        if ((status.has_value() &&
             !IsValidStatus(status.value())) ||
            page == 0 ||
            pageSize == 0 ||
            pageSize > kMaxPageSize)
        {
            return ServiceResult<ApplicationPage>{
                ErrorCode::kInvalidParam,
                {}};
        }

        const std::size_t pageIndex = page - 1;

        if (pageIndex >
            std::numeric_limits<std::size_t>::max() /
                pageSize)
        {
            return ServiceResult<ApplicationPage>{
                ErrorCode::kInvalidParam,
                {}};
        }

        try
        {
            ApplicationPage result;
            result.page = page;
            result.pageSize = pageSize;
            result.total =
                applicationDao_.Count(
                    userId,
                    status);
            result.list =
                applicationDao_.List(
                    userId,
                    status,
                    pageSize,
                    pageIndex * pageSize);

            return ServiceResult<ApplicationPage>{
                ErrorCode::kSuccess,
                std::move(result)};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "application list failed: {}",
                error.what());

            return ServiceResult<ApplicationPage>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

    ServiceResult<std::int32_t>
    ApplicationService::UpdateStatus(
        std::uint64_t userId,
        std::uint64_t applicationId,
        std::int32_t targetStatus)
    {
        if (userId == 0)
        {
            return ServiceResult<std::int32_t>{
                ErrorCode::kUnauthorized,
                0};
        }

        if (applicationId == 0 ||
            !IsValidStatus(targetStatus))
        {
            return ServiceResult<std::int32_t>{
                ErrorCode::kInvalidParam,
                0};
        }

        try
        {
            auto transaction =
                pool_.BeginTransaction();

            const std::optional<ApplicationRecord> application =
                applicationDao_.FindOwnedForUpdate(
                    transaction,
                    userId,
                    applicationId);

            if (!application.has_value())
            {
                return ServiceResult<std::int32_t>{
                    ErrorCode::kApplicationNotFound,
                    0};
            }

            if (!IsValidTransition(
                    application->status,
                    targetStatus))
            {
                return ServiceResult<std::int32_t>{
                    ErrorCode::kInvalidStatusTransition,
                    0};
            }

            const bool setApplyTime =
                targetStatus == kApplied &&
                !application->applyTime.has_value();

            if (!applicationDao_.UpdateStatus(
                    transaction,
                    userId,
                    applicationId,
                    targetStatus,
                    setApplyTime))
            {
                return ServiceResult<std::int32_t>{
                    ErrorCode::kDatabaseError,
                    0};
            }

            transaction.Commit();

            return ServiceResult<std::int32_t>{
                ErrorCode::kSuccess,
                targetStatus};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "application status update failed: {}",
                error.what());

            return ServiceResult<std::int32_t>{
                ErrorCode::kDatabaseError,
                0};
        }
    }

    ServiceResult<ApplicationStatistics>
    ApplicationService::Statistics(
        std::uint64_t userId)
    {
        if (userId == 0)
        {
            return ServiceResult<ApplicationStatistics>{
                ErrorCode::kUnauthorized,
                {}};
        }

        try
        {
            return ServiceResult<ApplicationStatistics>{
                ErrorCode::kSuccess,
                applicationDao_.Statistics(userId)};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "application statistics failed: {}",
                error.what());

            return ServiceResult<ApplicationStatistics>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

} // namespace jobpilot::service
