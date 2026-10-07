#pragma once

#include "jobpilot/service/resume/resume_dao_interface.h"

namespace jobpilot::core
{
    class ConnectionPool;
}

namespace jobpilot::service
{
    class ResumeDao final : public IResumeDao
    {
    public:
        explicit ResumeDao(
            jobpilot::core::ConnectionPool &pool);

        std::optional<std::uint64_t> CreateResume(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t userId,
            const std::string &title) override;

        bool LockOwnedResume(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t userId,
            std::uint64_t resumeId) override;

        std::int32_t NextVersion(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t resumeId) override;

        std::optional<std::uint64_t> CreateVersion(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t resumeId,
            std::int32_t version,
            const std::optional<std::string> &filePath,
            const std::optional<std::string> &content) override;

        std::vector<ResumeRecord> ListResumes(
            std::uint64_t userId,
            std::size_t limit,
            std::size_t offset) override;

        std::size_t CountResumes(
            std::uint64_t userId) override;

        std::optional<ResumeRecord> FindOwnedResume(
            std::uint64_t userId,
            std::uint64_t resumeId) override;

        std::vector<ResumeVersionRecord> ListVersions(
            std::uint64_t userId,
            std::uint64_t resumeId) override;

    private:
        jobpilot::core::ConnectionPool &pool_;
    };

} // namespace jobpilot::service
