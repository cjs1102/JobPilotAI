#include "jobpilot/service/resume/resume_artifact_service.h"

#include <exception>
#include <string>
#include <utility>

#include "jobpilot/core/logger.h"
#include "jobpilot/service/resume/tailor_rule.h"
#include "jobpilot/service/job/job_visibility.h"

namespace jobpilot::service
{
    ResumeArtifactService::ResumeArtifactService(
        IResumeArtifactDao &artifactDao,
        IResumeDao &resumeDao,
        IUserProfileReader &profileReader,
        IJobDao &jobDao,
        jobpilot::core::ConnectionPool &pool)
        : artifactDao_(artifactDao),
          resumeDao_(resumeDao),
          profileReader_(profileReader),
          jobDao_(jobDao),
          pool_(pool)
    {
    }

    ServiceResult<ResumeArtifactRecord>
    ResumeArtifactService::CreateDraft(
        std::uint64_t userId,
        std::uint64_t baseVersionId,
        const std::optional<std::uint64_t> &jobId,
        const std::optional<std::uint64_t> &applicationId)
    {
        if (userId == 0)
        {
            return ServiceResult<ResumeArtifactRecord>{
                ErrorCode::kUnauthorized,
                {}};
        }

        if (baseVersionId == 0)
        {
            return ServiceResult<ResumeArtifactRecord>{
                ErrorCode::kInvalidParam,
                {}};
        }

        try
        {
            auto transaction = pool_.BeginTransaction();

            switch (artifactDao_.CheckOwnership(
                transaction,
                userId,
                baseVersionId,
                applicationId))
            {
            case ArtifactOwnership::kBaseVersionNotFound:
                return ServiceResult<ResumeArtifactRecord>{
                    ErrorCode::kResumeNotFound,
                    {}};
            case ArtifactOwnership::kApplicationNotFound:
                return ServiceResult<ResumeArtifactRecord>{
                    ErrorCode::kApplicationNotFound,
                    {}};
            case ArtifactOwnership::kOk:
                break;
            }

            const std::optional<ResumeVersionRecord> version =
                resumeDao_.FindOwnedVersion(
                    userId,
                    baseVersionId);

            if (!version.has_value())
            {
                return ServiceResult<ResumeArtifactRecord>{
                    ErrorCode::kResumeNotFound,
                    {}};
            }

            const std::optional<UserProfile> profile =
                profileReader_.FindProfile(userId);
            const std::vector<UserSkill> skills =
                profileReader_.ListSkills(userId);

            JobSummary job;
            std::vector<JobRequirement> requirements;

            if (jobId.has_value())
            {
                const std::optional<JobDetail> detail =
                    jobDao_.FindPublicJobById(
                        jobId.value(),
                        PublicVisibilityIncludesNeedsReview());

                if (!detail.has_value())
                {
                    return ServiceResult<ResumeArtifactRecord>{
                        ErrorCode::kJobNotFound,
                        {}};
                }

                job = detail->summary;
                requirements = detail->requirements;
            }

            // The candidate name/contact are not part of the delivered seams
            // (IUserProfileReader exposes only the profile and skills), so the
            // draft renders them as explicit 【请填写：…】 placeholders.
            const TailorDraft draft =
                BuildTailorDraft(
                    std::string(),
                    std::string(),
                    profile.has_value()
                        ? profile.value()
                        : UserProfile{},
                    skills,
                    version->content.has_value()
                        ? version->content.value()
                        : std::string(),
                    job,
                    requirements);

            const FactCheckResult check = FactCheck(draft);

            ResumeArtifactWrite write;
            write.userId = userId;
            write.jobId = jobId;
            write.applicationId = applicationId;
            write.baseVersionId = baseVersionId;
            write.artifactType = "resume";
            write.language = "zh";
            write.content = draft.content;
            write.generator = kTailorGenerator;
            write.sourceVersion = kTailorSourceVersion;
            write.factCheckStatus = check.status;
            write.factCheckReport = check.reportJson;

            const std::optional<std::uint64_t> created =
                artifactDao_.Create(transaction, write);

            if (!created.has_value())
            {
                return ServiceResult<ResumeArtifactRecord>{
                    ErrorCode::kDatabaseError,
                    {}};
            }

            transaction.Commit();

            return Get(userId, created.value());
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "resume artifact draft failed: {}",
                error.what());

            return ServiceResult<ResumeArtifactRecord>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

    ServiceResult<std::vector<ResumeArtifactRecord>>
    ResumeArtifactService::List(
        std::uint64_t userId)
    {
        if (userId == 0)
        {
            return ServiceResult<
                std::vector<ResumeArtifactRecord>>{
                ErrorCode::kUnauthorized,
                {}};
        }

        try
        {
            return ServiceResult<
                std::vector<ResumeArtifactRecord>>{
                ErrorCode::kSuccess,
                artifactDao_.List(userId)};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "resume artifact list failed: {}",
                error.what());

            return ServiceResult<
                std::vector<ResumeArtifactRecord>>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

    ServiceResult<ResumeArtifactRecord>
    ResumeArtifactService::Get(
        std::uint64_t userId,
        std::uint64_t artifactId)
    {
        if (userId == 0)
        {
            return ServiceResult<ResumeArtifactRecord>{
                ErrorCode::kUnauthorized,
                {}};
        }

        try
        {
            const std::optional<ResumeArtifactRecord> stored =
                artifactDao_.Find(userId, artifactId);

            if (!stored.has_value())
            {
                return ServiceResult<ResumeArtifactRecord>{
                    ErrorCode::kResumeNotFound,
                    {}};
            }

            return ServiceResult<ResumeArtifactRecord>{
                ErrorCode::kSuccess,
                stored.value()};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "resume artifact read failed: {}",
                error.what());

            return ServiceResult<ResumeArtifactRecord>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

    ServiceResult<ResumeArtifactRecord>
    ResumeArtifactService::Confirm(
        std::uint64_t userId,
        std::uint64_t artifactId)
    {
        if (userId == 0)
        {
            return ServiceResult<ResumeArtifactRecord>{
                ErrorCode::kUnauthorized,
                {}};
        }

        try
        {
            const std::optional<ResumeArtifactRecord> stored =
                artifactDao_.Find(userId, artifactId);

            if (!stored.has_value())
            {
                return ServiceResult<ResumeArtifactRecord>{
                    ErrorCode::kResumeNotFound,
                    {}};
            }

            // A failed fact check can never be confirmed, and confirmation is a
            // one-way state change.
            if (stored->factCheckStatus == 2 ||
                stored->confirmedAt.has_value())
            {
                return ServiceResult<ResumeArtifactRecord>{
                    ErrorCode::kInvalidStatusTransition,
                    {}};
            }

            auto transaction = pool_.BeginTransaction();

            if (!artifactDao_.Confirm(
                    transaction,
                    userId,
                    artifactId))
            {
                return ServiceResult<ResumeArtifactRecord>{
                    ErrorCode::kInvalidStatusTransition,
                    {}};
            }

            transaction.Commit();

            return Get(userId, artifactId);
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "resume artifact confirmation failed: {}",
                error.what());

            return ServiceResult<ResumeArtifactRecord>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }
} // namespace jobpilot::service
