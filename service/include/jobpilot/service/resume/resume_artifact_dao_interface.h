#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "jobpilot/database/mysql_pool.h"

namespace jobpilot::service
{
    // 0015 D3: a stored tailored artifact. `confirmedAt` empty means DRAFT
    // (docs/03 section 10.6); non-empty means a human confirmed it. Confirmation
    // is a state change only -- nothing here submits anything anywhere.
    struct ResumeArtifactRecord
    {
        std::uint64_t id = 0;
        std::uint64_t userId = 0;
        std::optional<std::uint64_t> jobId;
        std::optional<std::uint64_t> applicationId;
        std::uint64_t baseVersionId = 0;
        std::string artifactType{"resume"};
        std::string language{"zh"};
        std::optional<std::string> content;
        std::optional<std::uint32_t> contentBytes;
        std::string generator{"rule_v1"};
        std::optional<std::string> sourceVersion;
        std::int32_t factCheckStatus = 0;
        std::optional<std::string> factCheckReport;
        std::optional<std::string> confirmedAt;
        std::string createTime;
        std::string updateTime;
    };

    struct ResumeArtifactWrite
    {
        std::uint64_t userId = 0;
        std::optional<std::uint64_t> jobId;
        std::optional<std::uint64_t> applicationId;
        std::uint64_t baseVersionId = 0;
        std::string artifactType{"resume"};
        std::string language{"zh"};
        std::optional<std::string> content;
        std::string generator{"rule_v1"};
        std::optional<std::string> sourceVersion;
        std::int32_t factCheckStatus = 0;
        std::optional<std::string> factCheckReport;
    };

    // Ownership outcome; the not-found values map onto the existing not-found
    // semantics at the service boundary.
    enum class ArtifactOwnership
    {
        kOk,
        kBaseVersionNotFound,
        kApplicationNotFound
    };

    class IResumeArtifactDao
    {
    public:
        virtual ~IResumeArtifactDao() = default;

        virtual ArtifactOwnership CheckOwnership(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t userId,
            std::uint64_t baseVersionId,
            const std::optional<std::uint64_t> &applicationId) = 0;

        virtual std::optional<std::uint64_t> Create(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            const ResumeArtifactWrite &write) = 0;

        virtual std::optional<ResumeArtifactRecord> Find(
            std::uint64_t userId,
            std::uint64_t artifactId) = 0;

        virtual std::vector<ResumeArtifactRecord> List(
            std::uint64_t userId) = 0;

        // Sets confirmed_at only when the artifact passes the fact gate
        // (fact_check_status != 2). Never sends anything.
        virtual bool Confirm(
            jobpilot::core::ConnectionPool::Transaction &transaction,
            std::uint64_t userId,
            std::uint64_t artifactId) = 0;
    };
}
