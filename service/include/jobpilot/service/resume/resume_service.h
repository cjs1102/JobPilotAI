#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "jobpilot/service/auth/session_authenticator.h"
#include "jobpilot/service/resume/resume_dao_interface.h"
#include "jobpilot/service/result.h"

namespace jobpilot::core
{
    class ConnectionPool;
    class FileStorage;
}

namespace jobpilot::service
{
    struct ResumeUpload
    {
        std::string title;
        std::string filename;
        std::string_view bytes;
        std::optional<std::string> contentType;
    };

    struct ResumeUploadResult
    {
        std::uint64_t resumeId = 0;
        std::int32_t version = 0;
    };

    struct ResumeVersionResult
    {
        std::uint64_t versionId = 0;
        std::int32_t version = 0;
    };

    struct ResumePage
    {
        std::vector<ResumeRecord> list;
        std::size_t total = 0;
        std::size_t page = 1;
        std::size_t pageSize = 20;
    };

    class ResumeService
    {
    public:
        ResumeService(
            IResumeDao &resumeDao,
            jobpilot::core::ConnectionPool &pool,
            jobpilot::core::FileStorage &storage,
            ISessionAuthenticator &authenticator,
            std::size_t maxFileBytes);

        ServiceResult<std::uint64_t> Authenticate(
            const std::string &token);

        ServiceResult<ResumeUploadResult> Upload(
            std::uint64_t userId,
            const ResumeUpload &upload);

        ServiceResult<ResumePage> ListResumes(
            std::uint64_t userId,
            std::size_t page,
            std::size_t pageSize);

        ServiceResult<ResumeVersionResult> CreateVersion(
            std::uint64_t userId,
            std::uint64_t resumeId,
            const std::string &content);

        ServiceResult<std::vector<ResumeVersionRecord>>
        ListVersions(
            std::uint64_t userId,
            std::uint64_t resumeId);

    private:
        static bool IsSupportedExtension(
            const std::string &extension);

        static std::optional<std::string> GetExtension(
            const std::string &filename);

        IResumeDao &resumeDao_;
        jobpilot::core::ConnectionPool &pool_;
        jobpilot::core::FileStorage &storage_;
        ISessionAuthenticator &authenticator_;
        std::size_t maxFileBytes_;
    };

} // namespace jobpilot::service
