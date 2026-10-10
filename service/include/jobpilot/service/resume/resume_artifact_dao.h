#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "jobpilot/database/mysql_pool.h"
#include "jobpilot/service/resume/resume_artifact_dao_interface.h"
#include "jobpilot/service/resume/resume_dao_interface.h"

namespace jobpilot::service
{
    // 0015 D3: the tailored-artifact store. Ownership of `base_version_id` is
    // validated through the existing `IResumeDao::OwnedVersionExists` seam (no
    // second validation path); `application_id` ownership is checked here.
    class ResumeArtifactDao final
        : public IResumeArtifactDao
    {
    public:
        ResumeArtifactDao(
            jobpilot::core::ConnectionPool &pool,
            IResumeDao &resumeDao);

        ArtifactOwnership CheckOwnership(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t userId,
            std::uint64_t baseVersionId,
            const std::optional<std::uint64_t> &applicationId) override;

        std::optional<std::uint64_t> Create(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            const ResumeArtifactWrite &write) override;

        std::optional<ResumeArtifactRecord> Find(
            std::uint64_t userId,
            std::uint64_t artifactId) override;

        std::vector<ResumeArtifactRecord> List(
            std::uint64_t userId) override;

        bool Confirm(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t userId,
            std::uint64_t artifactId) override;

    private:
        jobpilot::core::ConnectionPool &pool_;
        IResumeDao &resumeDao_;
    };

} // namespace jobpilot::service
