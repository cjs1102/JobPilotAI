#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "jobpilot/database/mysql_pool.h"

namespace jobpilot::service
{
    struct ResumeRecord
    {
        std::uint64_t id = 0;
        std::uint64_t userId = 0;
        std::string title;
        std::int32_t latestVersion = 0;
        std::string createTime;
        std::string updateTime;
    };

    struct ResumeVersionRecord
    {
        std::uint64_t id = 0;
        std::uint64_t resumeId = 0;
        std::int32_t version = 0;
        std::optional<std::string> filePath;
        std::optional<std::string> content;
        std::string createTime;
    };

    class IResumeDao
    {
    public:
        virtual ~IResumeDao() = default;

        virtual std::optional<std::uint64_t> CreateResume(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t userId,
            const std::string &title) = 0;

        virtual bool LockOwnedResume(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t userId,
            std::uint64_t resumeId) = 0;

        virtual std::int32_t NextVersion(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t resumeId) = 0;

        virtual std::optional<std::uint64_t> CreateVersion(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t resumeId,
            std::int32_t version,
            const std::optional<std::string> &filePath,
            const std::optional<std::string> &content) = 0;

        virtual std::vector<ResumeRecord> ListResumes(
            std::uint64_t userId,
            std::size_t limit,
            std::size_t offset) = 0;

        virtual std::size_t CountResumes(
            std::uint64_t userId) = 0;

        virtual std::optional<ResumeRecord> FindOwnedResume(
            std::uint64_t userId,
            std::uint64_t resumeId) = 0;

        virtual bool OwnedVersionExists(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t userId,
            std::uint64_t resumeVersionId) = 0;

        virtual std::vector<ResumeVersionRecord> ListVersions(
            std::uint64_t userId,
            std::uint64_t resumeId) = 0;
    };

} // namespace jobpilot::service
