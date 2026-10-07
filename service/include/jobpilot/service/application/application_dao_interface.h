#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "jobpilot/database/mysql_pool.h"

namespace jobpilot::service
{
    enum class ApplicationCreateStatus
    {
        kCreated,
        kDuplicate
    };

    struct ApplicationCreateResult
    {
        ApplicationCreateStatus status =
            ApplicationCreateStatus::kCreated;
        std::uint64_t applicationId = 0;
    };

    struct ApplicationRecord
    {
        std::uint64_t id = 0;
        std::uint64_t userId = 0;
        std::uint64_t jobId = 0;
        std::uint64_t resumeVersionId = 0;
        std::int32_t status = 0;
        std::optional<std::string> applyTime;
        std::optional<std::string> remark;
        std::string createTime;
        std::string updateTime;
    };

    struct ApplicationListItem
    {
        std::uint64_t id = 0;
        std::uint64_t jobId = 0;
        std::string jobTitle;
        std::string company;
        std::int32_t status = 0;
        std::optional<std::string> applyTime;
        std::optional<std::string> remark;
    };

    struct ApplicationStatistics
    {
        std::size_t total = 0;
        std::size_t interview = 0;
        std::size_t offer = 0;
        std::size_t rejected = 0;
    };

    class IApplicationDao
    {
    public:
        virtual ~IApplicationDao() = default;

        virtual ApplicationCreateResult Create(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t userId,
            std::uint64_t jobId,
            std::uint64_t resumeVersionId) = 0;

        virtual std::optional<ApplicationRecord> FindOwnedForUpdate(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t userId,
            std::uint64_t applicationId) = 0;

        virtual std::vector<ApplicationListItem> List(
            std::uint64_t userId,
            const std::optional<std::int32_t> &status,
            std::size_t limit,
            std::size_t offset) = 0;

        virtual std::size_t Count(
            std::uint64_t userId,
            const std::optional<std::int32_t> &status) = 0;

        virtual bool UpdateStatus(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t userId,
            std::uint64_t applicationId,
            std::int32_t status,
            bool setApplyTime) = 0;

        virtual ApplicationStatistics Statistics(
            std::uint64_t userId) = 0;
    };

} // namespace jobpilot::service
