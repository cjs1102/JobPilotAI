#pragma once

#include "jobpilot/service/application/application_dao_interface.h"

namespace jobpilot::core
{
    class ConnectionPool;
}

namespace jobpilot::service
{
    class ApplicationDao final : public IApplicationDao
    {
    public:
        explicit ApplicationDao(
            jobpilot::core::ConnectionPool &pool);

        ApplicationCreateResult Create(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t userId,
            std::uint64_t jobId,
            std::uint64_t resumeVersionId) override;

        std::optional<ApplicationRecord> FindOwnedForUpdate(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t userId,
            std::uint64_t applicationId) override;

        std::vector<ApplicationListItem> List(
            std::uint64_t userId,
            const std::optional<std::int32_t> &status,
            std::size_t limit,
            std::size_t offset) override;

        std::size_t Count(
            std::uint64_t userId,
            const std::optional<std::int32_t> &status) override;

        bool UpdateStatus(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t userId,
            std::uint64_t applicationId,
            std::int32_t status,
            bool setApplyTime) override;

        ApplicationStatistics Statistics(
            std::uint64_t userId) override;

    private:
        jobpilot::core::ConnectionPool &pool_;
    };

} // namespace jobpilot::service
