#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "jobpilot/database/mysql_pool.h"
#include "jobpilot/service/job/job_dao_interface.h"
#include "jobpilot/service/resume/resume_artifact_dao_interface.h"
#include "jobpilot/service/resume/resume_dao_interface.h"
#include "jobpilot/service/result.h"
#include "jobpilot/service/user/user_dao_interface.h"

namespace jobpilot::service
{
    // 0015 phase D step D5: the tailored-artifact surface. `CreateDraft` runs the
    // deterministic `tailor_rule_v1` draft + the mechanical fact gate and stores
    // the result; `Confirm` is a pure state change (it never sends anything).
    class ResumeArtifactService
    {
    public:
        ResumeArtifactService(
            IResumeArtifactDao &artifactDao,
            IResumeDao &resumeDao,
            IUserProfileReader &profileReader,
            IJobDao &jobDao,
            jobpilot::core::ConnectionPool &pool);

        ServiceResult<ResumeArtifactRecord> CreateDraft(
            std::uint64_t userId,
            std::uint64_t baseVersionId,
            const std::optional<std::uint64_t> &jobId,
            const std::optional<std::uint64_t> &applicationId);

        ServiceResult<std::vector<ResumeArtifactRecord>> List(
            std::uint64_t userId);

        ServiceResult<ResumeArtifactRecord> Get(
            std::uint64_t userId,
            std::uint64_t artifactId);

        ServiceResult<ResumeArtifactRecord> Confirm(
            std::uint64_t userId,
            std::uint64_t artifactId);

    private:
        IResumeArtifactDao &artifactDao_;
        IResumeDao &resumeDao_;
        IUserProfileReader &profileReader_;
        IJobDao &jobDao_;
        jobpilot::core::ConnectionPool &pool_;
    };

} // namespace jobpilot::service
