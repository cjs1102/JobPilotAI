#include "jobpilot/service/resume/resume_service.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>

#include "jobpilot/core/logger.h"
#include "jobpilot/core/utf8.h"
#include "jobpilot/database/mysql_pool.h"
#include "jobpilot/storage/file_storage.h"

namespace jobpilot::service
{
    namespace
    {
        constexpr std::size_t kMaxTitleBytes = 128;
        constexpr std::size_t kMaxVersionContentBytes =
            1U * 1024U * 1024U;

        enum class UploadStage
        {
            kStorage,
            kDatabase,
            kPromotion,
            kCommit
        };

        std::string ToLower(std::string value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](unsigned char character)
                {
                    return static_cast<char>(
                        std::tolower(character));
                });

            return value;
        }

        bool IsPdf(
            std::string_view bytes)
        {
            constexpr std::string_view kPdfMagic =
                "%PDF-";

            return bytes.size() >= kPdfMagic.size() &&
                   bytes.substr(0, kPdfMagic.size()) ==
                       kPdfMagic;
        }

        std::string BuildRelativePath(
            std::uint64_t userId,
            std::uint64_t resumeId,
            std::int32_t version,
            const std::string &extension)
        {
            return std::to_string(userId) +
                   "/resumes/" +
                   std::to_string(resumeId) +
                   "/v" +
                   std::to_string(version) +
                   extension;
        }
    } // namespace

    ResumeService::ResumeService(
        IResumeDao &resumeDao,
        jobpilot::core::ConnectionPool &pool,
        jobpilot::core::FileStorage &storage,
        ISessionAuthenticator &authenticator,
        std::size_t maxFileBytes)
        : resumeDao_(resumeDao),
          pool_(pool),
          storage_(storage),
          authenticator_(authenticator),
          maxFileBytes_(maxFileBytes)
    {
        if (maxFileBytes_ == 0)
        {
            throw std::invalid_argument(
                "max resume file size must be greater than zero");
        }
    }

    ServiceResult<std::uint64_t> ResumeService::Authenticate(
        const std::string &token)
    {
        return authenticator_.Authenticate(token);
    }

    std::optional<std::string> ResumeService::GetExtension(
        const std::string &filename)
    {
        const std::size_t position =
            filename.find_last_of('.');

        if (position == std::string::npos ||
            position + 1 == filename.size())
        {
            return std::nullopt;
        }

        return ToLower(
            filename.substr(position));
    }

    bool ResumeService::IsSupportedExtension(
        const std::string &extension)
    {
        return extension == ".pdf" ||
               extension == ".txt" ||
               extension == ".md";
    }

    ServiceResult<ResumeUploadResult> ResumeService::Upload(
        std::uint64_t userId,
        const ResumeUpload &upload)
    {
        if (userId == 0)
        {
            return ServiceResult<ResumeUploadResult>{
                ErrorCode::kUnauthorized,
                {}};
        }

        const std::optional<std::string> extension =
            GetExtension(upload.filename);

        if (upload.title.empty() ||
            upload.title.size() > kMaxTitleBytes ||
            !jobpilot::core::IsValidUtf8(upload.title) ||
            !jobpilot::core::IsValidUtf8(upload.filename) ||
            !extension.has_value() ||
            !IsSupportedExtension(extension.value()) ||
            upload.bytes.empty() ||
            upload.bytes.size() > maxFileBytes_ ||
            (extension.value() == ".pdf" &&
             !IsPdf(upload.bytes)))
        {
            return ServiceResult<ResumeUploadResult>{
                ErrorCode::kInvalidParam,
                {}};
        }

        const std::optional<std::string> content =
            extension.value() == ".pdf"
                ? std::nullopt
                : std::optional<std::string>(
                      std::string(upload.bytes));

        // Text resume bytes are persisted as version content. Validate them
        // before the temp file or transaction is created so an invalid client
        // value cannot surface as a database error.
        if (content.has_value() &&
            !jobpilot::core::IsValidUtf8(content.value()))
        {
            return ServiceResult<ResumeUploadResult>{
                ErrorCode::kInvalidParam,
                {}};
        }

        UploadStage stage = UploadStage::kStorage;
        bool tempCreated = false;
        bool promoted = false;
        bool commitAttempted = false;
        std::string relativePath;
        jobpilot::core::TempFile temp;

        try
        {
            temp = storage_.WriteTemp(upload.bytes);
            tempCreated = true;
            stage = UploadStage::kDatabase;

            auto transaction =
                pool_.BeginTransaction();

            const std::optional<std::uint64_t> resumeId =
                resumeDao_.CreateResume(
                    transaction,
                    userId,
                    upload.title);

            if (!resumeId.has_value() ||
                !resumeDao_.LockOwnedResume(
                    transaction,
                    userId,
                    resumeId.value()))
            {
                throw std::runtime_error(
                    "failed to create or lock resume metadata");
            }

            const std::int32_t version =
                resumeDao_.NextVersion(
                    transaction,
                    resumeId.value());

            relativePath = BuildRelativePath(
                userId,
                resumeId.value(),
                version,
                extension.value());

            if (!resumeDao_.CreateVersion(
                     transaction,
                     resumeId.value(),
                     version,
                     relativePath,
                     content)
                     .has_value())
            {
                throw std::runtime_error(
                    "failed to create resume version metadata");
            }

            stage = UploadStage::kPromotion;
            storage_.Promote(
                temp,
                relativePath);
            promoted = true;

            commitAttempted = true;
            stage = UploadStage::kCommit;
            transaction.Commit();

            return ServiceResult<ResumeUploadResult>{
                ErrorCode::kSuccess,
                ResumeUploadResult{
                    resumeId.value(),
                    version,
                    ClassifyResumeText(
                        content,
                        relativePath)}};
        }
        catch (const std::exception &error)
        {
            if (!commitAttempted)
            {
                if (promoted)
                {
                    try
                    {
                        storage_.Remove(relativePath);
                    }
                    catch (const std::exception &cleanupError)
                    {
                        jobpilot::core::Logger::Get()->error(
                            "resume file compensation failed: {}",
                            cleanupError.what());
                    }
                }

                if (tempCreated)
                {
                    storage_.RemoveTemp(temp);
                }
            }

            jobpilot::core::Logger::Get()->error(
                "resume upload failed: {}",
                error.what());

            return ServiceResult<ResumeUploadResult>{
                stage == UploadStage::kDatabase
                    ? ErrorCode::kDatabaseError
                    : ErrorCode::kInternalError,
                {}};
        }
    }

    ServiceResult<ResumePage> ResumeService::ListResumes(
        std::uint64_t userId,
        std::size_t page,
        std::size_t pageSize)
    {
        if (userId == 0)
        {
            return ServiceResult<ResumePage>{
                ErrorCode::kUnauthorized,
                {}};
        }

        if (page == 0 ||
            pageSize == 0 ||
            pageSize > 100)
        {
            return ServiceResult<ResumePage>{
                ErrorCode::kInvalidParam,
                {}};
        }

        try
        {
            ResumePage result;
            result.page = page;
            result.pageSize = pageSize;
            result.total =
                resumeDao_.CountResumes(userId);
            result.list =
                resumeDao_.ListResumes(
                    userId,
                    pageSize,
                    (page - 1) * pageSize);

            return ServiceResult<ResumePage>{
                ErrorCode::kSuccess,
                std::move(result)};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "resume list failed: {}",
                error.what());

            return ServiceResult<ResumePage>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

    ServiceResult<ResumeVersionResult>
    ResumeService::CreateVersion(
        std::uint64_t userId,
        std::uint64_t resumeId,
        const std::string &content)
    {
        if (userId == 0)
        {
            return ServiceResult<ResumeVersionResult>{
                ErrorCode::kUnauthorized,
                {}};
        }

        if (resumeId == 0 ||
            content.empty() ||
            content.size() > kMaxVersionContentBytes)
        {
            return ServiceResult<ResumeVersionResult>{
                ErrorCode::kInvalidParam,
                {}};
        }

        try
        {
            auto transaction =
                pool_.BeginTransaction();

            if (!resumeDao_.LockOwnedResume(
                    transaction,
                    userId,
                    resumeId))
            {
                return ServiceResult<ResumeVersionResult>{
                    ErrorCode::kResumeNotFound,
                    {}};
            }

            const std::int32_t version =
                resumeDao_.NextVersion(
                    transaction,
                    resumeId);

            const std::optional<std::uint64_t> versionId =
                resumeDao_.CreateVersion(
                    transaction,
                    resumeId,
                    version,
                    std::nullopt,
                    content);

            if (!versionId.has_value())
            {
                return ServiceResult<ResumeVersionResult>{
                    ErrorCode::kDatabaseError,
                    {}};
            }

            transaction.Commit();

            return ServiceResult<ResumeVersionResult>{
                ErrorCode::kSuccess,
                ResumeVersionResult{
                    versionId.value(),
                    version,
                    ClassifyResumeText(
                        content,
                        std::string())}};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "resume version creation failed: {}",
                error.what());

            return ServiceResult<ResumeVersionResult>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

    ServiceResult<std::vector<ResumeVersionRecord>>
    ResumeService::ListVersions(
        std::uint64_t userId,
        std::uint64_t resumeId)
    {
        if (userId == 0)
        {
            return ServiceResult<
                std::vector<ResumeVersionRecord>>{
                ErrorCode::kUnauthorized,
                {}};
        }

        if (resumeId == 0)
        {
            return ServiceResult<
                std::vector<ResumeVersionRecord>>{
                ErrorCode::kInvalidParam,
                {}};
        }

        try
        {
            if (!resumeDao_.FindOwnedResume(
                     userId,
                     resumeId)
                     .has_value())
            {
                return ServiceResult<
                    std::vector<ResumeVersionRecord>>{
                    ErrorCode::kResumeNotFound,
                    {}};
            }

            return ServiceResult<
                std::vector<ResumeVersionRecord>>{
                ErrorCode::kSuccess,
                resumeDao_.ListVersions(
                    userId,
                    resumeId)};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "resume version list failed: {}",
                error.what());

            return ServiceResult<
                std::vector<ResumeVersionRecord>>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

} // namespace jobpilot::service
