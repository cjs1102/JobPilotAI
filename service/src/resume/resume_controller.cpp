#include "jobpilot/service/resume/resume_controller.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include "jobpilot/error.h"
#include "jobpilot/service/request_validation.h"
#include "jobpilot/http/multipart_form_data_parser.h"

namespace jobpilot::service
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::size_t kDefaultPage = 1;
        constexpr std::size_t kDefaultPageSize = 20;

        int HttpStatusFor(ErrorCode code)
        {
            switch (code)
            {
            case ErrorCode::kSuccess:
                return 200;
            case ErrorCode::kInvalidParam:
            case ErrorCode::kMalformedRequest:
                return 400;
            case ErrorCode::kUnauthorized:
            case ErrorCode::kTokenExpired:
                return 401;
            case ErrorCode::kForbidden:
                return 403;
            case ErrorCode::kJobNotFound:
            case ErrorCode::kResumeNotFound:
            case ErrorCode::kApplicationNotFound:
            case ErrorCode::kAiTaskNotFound:
                return 404;
            case ErrorCode::kEmailExists:
            case ErrorCode::kDuplicateApplication:
                return 409;
            // 0015 D5: an artifact that cannot change state (already confirmed,
            // or blocked by a failed fact check) is a conflict. This code had no
            // producer in the resume flow before D5; the mapping now matches the
            // internal review controller's.
            case ErrorCode::kInvalidStatusTransition:
                return 409;
            case ErrorCode::kUserNotFound:
            case ErrorCode::kInvalidPassword:
                return 401;
            // Same mapping as the AI and internal controllers; kept exhaustive
            // with a fallback so future enumerators cannot reintroduce the
            // -Wswitch warning.
            case ErrorCode::kAiServiceBusy:
                return 503;
            case ErrorCode::kAiAnalysisFailed:
            case ErrorCode::kInternalError:
            case ErrorCode::kDatabaseError:
                return 500;
            default:
                return 500;
            }

            return 500;
        }

        const char *HttpReason(int statusCode)
        {
            switch (statusCode)
            {
            case 200:
                return "OK";
            case 400:
                return "Bad Request";
            case 401:
                return "Unauthorized";
            case 403:
                return "Forbidden";
            case 404:
                return "Not Found";
            case 409:
                return "Conflict";
            case 503:
                return "Service Unavailable";
            default:
                return "Internal Server Error";
            }
        }

        void WriteResponse(
            jobpilot::core::HttpResponse *response,
            bool keepAlive,
            ErrorCode code,
            const Json &data)
        {
            const int statusCode =
                HttpStatusFor(code);

            response->SetStatusCode(statusCode);
            response->SetStatusMessage(
                HttpReason(statusCode));
            response->SetHeader(
                "Content-Type",
                "application/json");
            response->SetBody(
                Json{
                    {"code",
                     static_cast<int>(code)},
                    {"message",
                     ErrorMessage(code)},
                    {"data", data}}
                    .dump());
            response->SetKeepAlive(keepAlive);
        }

        std::optional<std::uint64_t> AuthenticateRequest(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse *response,
            ResumeService *service)
        {
            const std::optional<std::string> token =
                ParseBearerToken(
                    request.GetHeader("Authorization"));

            if (!token.has_value())
            {
                WriteResponse(
                    response,
                    request.KeepAlive(),
                    ErrorCode::kUnauthorized,
                    nullptr);

                return std::nullopt;
            }

            const ServiceResult<std::uint64_t> result =
                service->Authenticate(token.value());

            if (result.code != ErrorCode::kSuccess)
            {
                WriteResponse(
                    response,
                    request.KeepAlive(),
                    result.code,
                    nullptr);

                return std::nullopt;
            }

            return result.value;
        }

        std::string PathWithoutQuery(
            const std::string &path)
        {
            const std::size_t queryPosition =
                path.find('?');

            return path.substr(
                0,
                queryPosition);
        }

        std::optional<std::uint64_t> ParseResumeId(
            const std::string &path)
        {
            constexpr const char *kPrefix =
                "/api/v1/resumes/";
            constexpr const char *kSuffix =
                "/versions";

            const std::string pathOnly =
                PathWithoutQuery(path);

            if (pathOnly.rfind(kPrefix, 0) != 0 ||
                pathOnly.size() <=
                    std::string(kPrefix).size() +
                        std::string(kSuffix).size() ||
                pathOnly.compare(
                    pathOnly.size() -
                        std::string(kSuffix).size(),
                    std::string(kSuffix).size(),
                    kSuffix) != 0)
            {
                return std::nullopt;
            }

            const std::string text =
                pathOnly.substr(
                    std::string(kPrefix).size(),
                    pathOnly.size() -
                        std::string(kPrefix).size() -
                        std::string(kSuffix).size());

            std::size_t parsed = 0;

            try
            {
                const unsigned long long value =
                    std::stoull(text, &parsed);

                if (parsed != text.size() ||
                    value == 0)
                {
                    return std::nullopt;
                }

                return static_cast<std::uint64_t>(
                    value);
            }
            catch (const std::exception &)
            {
                return std::nullopt;
            }
        }

        // 0015 D5: the artifact id is the segment after
        // `/api/v1/resumes/artifacts/`, optionally followed by a fixed suffix
        // (`/confirm`). A static route is registered before the `{id}` route.
        std::optional<std::uint64_t> ParseArtifactId(
            const std::string &path,
            const std::string &suffix)
        {
            constexpr const char *kPrefix =
                "/api/v1/resumes/artifacts/";

            const std::string pathOnly =
                PathWithoutQuery(path);

            if (pathOnly.rfind(kPrefix, 0) != 0 ||
                pathOnly.size() <=
                    std::string(kPrefix).size() +
                        suffix.size() ||
                pathOnly.compare(
                    pathOnly.size() - suffix.size(),
                    suffix.size(),
                    suffix) != 0)
            {
                return std::nullopt;
            }

            const std::string text = pathOnly.substr(
                std::string(kPrefix).size(),
                pathOnly.size() -
                    std::string(kPrefix).size() -
                    suffix.size());

            std::size_t parsed = 0;

            try
            {
                const unsigned long long value =
                    std::stoull(text, &parsed);

                if (parsed != text.size() || value == 0)
                {
                    return std::nullopt;
                }

                return static_cast<std::uint64_t>(value);
            }
            catch (const std::exception &)
            {
                return std::nullopt;
            }
        }

        bool TryGetRequiredUInt64(
            const Json &body,
            const char *key,
            std::uint64_t *value)
        {
            const auto iterator = body.find(key);

            if (iterator == body.end() ||
                !iterator->is_number_unsigned())
            {
                return false;
            }

            *value = iterator->get<std::uint64_t>();
            return true;
        }

        bool TryGetOptionalUInt64(
            const Json &body,
            const char *key,
            std::optional<std::uint64_t> *value)
        {
            const auto iterator = body.find(key);

            if (iterator == body.end() ||
                iterator->is_null())
            {
                value->reset();
                return true;
            }

            if (!iterator->is_number_unsigned())
            {
                return false;
            }

            *value = iterator->get<std::uint64_t>();
            return true;
        }

        std::optional<std::pair<std::size_t, std::size_t>>
        ParsePagination(
            const std::string &path)
        {
            std::size_t page = kDefaultPage;
            std::size_t pageSize = kDefaultPageSize;
            bool hasPage = false;
            bool hasPageSize = false;

            const std::size_t queryPosition =
                path.find('?');

            if (queryPosition == std::string::npos)
            {
                return std::make_pair(page, pageSize);
            }

            std::size_t position = queryPosition + 1;

            while (position <= path.size())
            {
                const std::size_t ampersand =
                    path.find('&', position);
                const std::size_t end =
                    ampersand == std::string::npos
                        ? path.size()
                        : ampersand;
                const std::string field =
                    path.substr(
                        position,
                        end - position);

                if (!field.empty())
                {
                    const std::size_t equals =
                        field.find('=');

                    if (equals == std::string::npos)
                    {
                        return std::nullopt;
                    }

                    const std::string key =
                        field.substr(0, equals);
                    const std::string value =
                        field.substr(equals + 1);

                    if (key != "page" &&
                        key != "page_size")
                    {
                        position = end + 1;
                        continue;
                    }

                    std::size_t parsed = 0;

                    try
                    {
                        const unsigned long long number =
                            std::stoull(value, &parsed);

                        if (parsed != value.size())
                        {
                            return std::nullopt;
                        }

                        if (key == "page")
                        {
                            if (hasPage)
                            {
                                return std::nullopt;
                            }

                            hasPage = true;
                            page =
                                static_cast<std::size_t>(
                                    number);
                        }
                        else
                        {
                            if (hasPageSize)
                            {
                                return std::nullopt;
                            }

                            hasPageSize = true;
                            pageSize =
                                static_cast<std::size_t>(
                                    number);
                        }
                    }
                    catch (const std::exception &)
                    {
                        return std::nullopt;
                    }
                }

                if (ampersand == std::string::npos)
                {
                    break;
                }

                position = ampersand + 1;
            }

            if (page == 0 ||
                pageSize == 0 ||
                pageSize > 100)
            {
                return std::nullopt;
            }

            return std::make_pair(page, pageSize);
        }

        bool TryGetString(
            const Json &body,
            const char *key,
            std::string *value)
        {
            const auto iterator =
                body.find(key);

            if (iterator == body.end() ||
                !iterator->is_string())
            {
                return false;
            }

            *value =
                iterator->get<std::string>();
            return true;
        }

        bool ParseJsonBody(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse *response,
            Json *body)
        {
            if (!jobpilot::core::IsValidUtf8(
                    request.Body()))
            {
                WriteResponse(
                    response,
                    request.KeepAlive(),
                    ErrorCode::kInvalidParam,
                    ParamError("body"));

                return false;
            }

            try
            {
                *body = Json::parse(
                    request.Body());
            }
            catch (const Json::parse_error &)
            {
                WriteResponse(
                    response,
                    request.KeepAlive(),
                    ErrorCode::kMalformedRequest,
                    nullptr);

                return false;
            }

            if (!body->is_object())
            {
                WriteResponse(
                    response,
                    request.KeepAlive(),
                    ErrorCode::kInvalidParam,
                    ParamError("body"));

                return false;
            }

            std::string invalidField;

            if (!ValidateJsonUtf8(*body, &invalidField))
            {
                WriteResponse(
                    response,
                    request.KeepAlive(),
                    ErrorCode::kInvalidParam,
                    ParamError(invalidField));

                return false;
            }

            return true;
        }

        std::optional<ResumeUpload> ParseUpload(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse *response)
        {
            jobpilot::core::MultipartFormData form;

            const jobpilot::core::MultipartParseResult result =
                jobpilot::core::MultipartFormDataParser::Parse(
                    request.GetHeader("Content-Type"),
                    request.Body(),
                    &form);

            if (result !=
                jobpilot::core::MultipartParseResult::kSuccess)
            {
                WriteResponse(
                    response,
                    request.KeepAlive(),
                    result ==
                            jobpilot::core::
                                MultipartParseResult::kLimitExceeded
                        ? ErrorCode::kInvalidParam
                        : ErrorCode::kMalformedRequest,
                    nullptr);

                return std::nullopt;
            }

            const jobpilot::core::MultipartPart *title = nullptr;
            const jobpilot::core::MultipartPart *file = nullptr;

            for (const jobpilot::core::MultipartPart &part :
                 form.parts)
            {
                if (part.name == "title")
                {
                    if (title != nullptr)
                    {
                        WriteResponse(
                            response,
                            request.KeepAlive(),
                            ErrorCode::kInvalidParam,
                            ParamError("title"));

                        return std::nullopt;
                    }

                    title = &part;
                }
                else if (part.name == "file")
                {
                    if (file != nullptr)
                    {
                        WriteResponse(
                            response,
                            request.KeepAlive(),
                            ErrorCode::kInvalidParam,
                            ParamError("file"));

                        return std::nullopt;
                    }

                    file = &part;
                }
            }

            if (title == nullptr)
            {
                WriteResponse(
                    response,
                    request.KeepAlive(),
                    ErrorCode::kInvalidParam,
                    ParamError("title"));

                return std::nullopt;
            }

            if (file == nullptr ||
                !file->filename.has_value())
            {
                WriteResponse(
                    response,
                    request.KeepAlive(),
                    ErrorCode::kInvalidParam,
                    ParamError("file"));

                return std::nullopt;
            }

            if (!jobpilot::core::IsValidUtf8(title->body))
            {
                WriteResponse(
                    response,
                    request.KeepAlive(),
                    ErrorCode::kInvalidParam,
                    ParamError("title"));

                return std::nullopt;
            }

            if (!jobpilot::core::IsValidUtf8(
                    file->filename.value()))
            {
                WriteResponse(
                    response,
                    request.KeepAlive(),
                    ErrorCode::kInvalidParam,
                    ParamError("file"));

                return std::nullopt;
            }

            ResumeUpload upload;
            upload.title =
                std::string(title->body);
            upload.filename =
                file->filename.value();
            upload.bytes = file->body;
            upload.contentType = file->contentType;

            return upload;
        }
    } // namespace

    ResumeController::ResumeController(
        ResumeService &service,
        ResumeArtifactService *artifactService)
        : service_(service),
          artifactService_(artifactService)
    {
    }

    void ResumeController::Upload(
        const jobpilot::core::HttpRequest &request,
        jobpilot::core::HttpResponse &response)
    {
        const std::optional<std::uint64_t> userId =
            AuthenticateRequest(
                request,
                &response,
                &service_);

        if (!userId.has_value())
        {
            return;
        }

        const std::optional<ResumeUpload> upload =
            ParseUpload(
                request,
                &response);

        if (!upload.has_value())
        {
            return;
        }

        const ServiceResult<ResumeUploadResult> result =
            service_.Upload(
                userId.value(),
                upload.value());

        if (result.code != ErrorCode::kSuccess)
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                result.code,
                nullptr);

            return;
        }

        WriteResponse(
            &response,
            request.KeepAlive(),
            ErrorCode::kSuccess,
            Json{
                {"resume_id", result.value.resumeId},
                {"version", result.value.version},
                // 0015 A3 (option b): never a silent "no text".
                {"text_extracted",
                 TextExtracted(result.value.textStatus)},
                {"text_status",
                 ToString(result.value.textStatus)}});
    }

    void ResumeController::List(
        const jobpilot::core::HttpRequest &request,
        jobpilot::core::HttpResponse &response)
    {
        const std::optional<std::uint64_t> userId =
            AuthenticateRequest(
                request,
                &response,
                &service_);

        if (!userId.has_value())
        {
            return;
        }

        const std::optional<
            std::pair<std::size_t, std::size_t>>
            pagination =
                ParsePagination(request.Path());

        if (!pagination.has_value())
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                ErrorCode::kInvalidParam,
                nullptr);

            return;
        }

        const ServiceResult<ResumePage> result =
            service_.ListResumes(
                userId.value(),
                pagination->first,
                pagination->second);

        if (result.code != ErrorCode::kSuccess)
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                result.code,
                nullptr);

            return;
        }

        Json list = Json::array();

        for (const ResumeRecord &resume :
             result.value.list)
        {
            list.push_back(
                Json{
                    {"id", resume.id},
                    {"title", resume.title},
                    {"version", resume.latestVersion},
                    {"created_at", resume.createTime},
                    {"updated_at", resume.updateTime}});
        }

        WriteResponse(
            &response,
            request.KeepAlive(),
            ErrorCode::kSuccess,
            Json{
                {"list", std::move(list)},
                {"total", result.value.total},
                {"page", result.value.page},
                {"page_size", result.value.pageSize}});
    }

    void ResumeController::CreateVersion(
        const jobpilot::core::HttpRequest &request,
        jobpilot::core::HttpResponse &response)
    {
        const std::optional<std::uint64_t> userId =
            AuthenticateRequest(
                request,
                &response,
                &service_);

        if (!userId.has_value())
        {
            return;
        }

        const std::optional<std::uint64_t> resumeId =
            ParseResumeId(request.Path());

        if (!resumeId.has_value())
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                ErrorCode::kInvalidParam,
                nullptr);

            return;
        }

        Json body;

        if (!ParseJsonBody(
                request,
                &response,
                &body))
        {
            return;
        }

        std::string content;

        if (!TryGetString(
                body,
                "content",
                &content))
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                ErrorCode::kInvalidParam,
                ParamError("content"));

            return;
        }

        const ServiceResult<ResumeVersionResult> result =
            service_.CreateVersion(
                userId.value(),
                resumeId.value(),
                content);

        if (result.code != ErrorCode::kSuccess)
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                result.code,
                nullptr);

            return;
        }

        WriteResponse(
            &response,
            request.KeepAlive(),
            ErrorCode::kSuccess,
            Json{
                {"version_id", result.value.versionId},
                {"version", result.value.version},
                {"text_extracted",
                 TextExtracted(result.value.textStatus)},
                {"text_status",
                 ToString(result.value.textStatus)}});
    }

    void ResumeController::ListVersions(
        const jobpilot::core::HttpRequest &request,
        jobpilot::core::HttpResponse &response)
    {
        const std::optional<std::uint64_t> userId =
            AuthenticateRequest(
                request,
                &response,
                &service_);

        if (!userId.has_value())
        {
            return;
        }

        const std::optional<std::uint64_t> resumeId =
            ParseResumeId(request.Path());

        if (!resumeId.has_value())
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                ErrorCode::kInvalidParam,
                nullptr);

            return;
        }

        const ServiceResult<std::vector<ResumeVersionRecord>>
            result =
                service_.ListVersions(
                    userId.value(),
                    resumeId.value());

        if (result.code != ErrorCode::kSuccess)
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                result.code,
                nullptr);

            return;
        }

        Json list = Json::array();

        for (const ResumeVersionRecord &version :
             result.value)
        {
            const ResumeTextStatus textStatus =
                ClassifyResumeText(
                    version.content,
                    version.filePath.has_value()
                        ? version.filePath.value()
                        : std::string());

            list.push_back(
                Json{
                    {"id", version.id},
                    {"version", version.version},
                    {"created_at", version.createTime},
                    {"text_extracted",
                     TextExtracted(textStatus)},
                    {"text_status", ToString(textStatus)}});
        }

        WriteResponse(
            &response,
            request.KeepAlive(),
            ErrorCode::kSuccess,
            Json{{"list", std::move(list)}});
    }

    namespace
    {
        // 0015 D5: every artifact response states the draft/confirmed boundary
        // and the fact-gate verdict explicitly.
        Json ArtifactToJson(
            const ResumeArtifactRecord &artifact)
        {
            Json report = Json(nullptr);

            if (artifact.factCheckReport.has_value())
            {
                report = Json::parse(
                    artifact.factCheckReport.value(),
                    nullptr,
                    false);
            }

            return Json{
                {"id", artifact.id},
                {"job_id",
                 artifact.jobId.has_value()
                     ? Json(artifact.jobId.value())
                     : Json(nullptr)},
                {"application_id",
                 artifact.applicationId.has_value()
                     ? Json(artifact.applicationId.value())
                     : Json(nullptr)},
                {"base_version_id", artifact.baseVersionId},
                {"artifact_type", artifact.artifactType},
                {"language", artifact.language},
                {"content",
                 artifact.content.has_value()
                     ? Json(artifact.content.value())
                     : Json(nullptr)},
                {"content_bytes",
                 artifact.contentBytes.has_value()
                     ? Json(artifact.contentBytes.value())
                     : Json(nullptr)},
                {"generator", artifact.generator},
                {"source_version",
                 artifact.sourceVersion.has_value()
                     ? Json(artifact.sourceVersion.value())
                     : Json(nullptr)},
                {"fact_check_status", artifact.factCheckStatus},
                {"fact_check_report", std::move(report)},
                {"confirmed", artifact.confirmedAt.has_value()},
                {"confirmed_at",
                 artifact.confirmedAt.has_value()
                     ? Json(artifact.confirmedAt.value())
                     : Json(nullptr)},
                {"created_at", artifact.createTime}};
        }
    } // namespace

    void ResumeController::CreateArtifact(
        const jobpilot::core::HttpRequest &request,
        jobpilot::core::HttpResponse &response)
    {
        const std::optional<std::uint64_t> userId =
            AuthenticateRequest(
                request,
                &response,
                &service_);

        if (!userId.has_value())
        {
            return;
        }

        if (artifactService_ == nullptr)
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                ErrorCode::kInternalError,
                nullptr);
            return;
        }

        Json body;

        if (!ParseJsonBody(
                request,
                &response,
                &body))
        {
            return;
        }

        std::uint64_t baseVersionId = 0;
        std::optional<std::uint64_t> jobId;
        std::optional<std::uint64_t> applicationId;

        if (!TryGetRequiredUInt64(
                body,
                "base_version_id",
                &baseVersionId) ||
            !TryGetOptionalUInt64(
                body,
                "job_id",
                &jobId) ||
            !TryGetOptionalUInt64(
                body,
                "application_id",
                &applicationId))
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                ErrorCode::kInvalidParam,
                nullptr);
            return;
        }

        const ServiceResult<ResumeArtifactRecord> result =
            artifactService_->CreateDraft(
                userId.value(),
                baseVersionId,
                jobId,
                applicationId);

        if (result.code != ErrorCode::kSuccess)
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                result.code,
                nullptr);
            return;
        }

        WriteResponse(
            &response,
            request.KeepAlive(),
            ErrorCode::kSuccess,
            ArtifactToJson(result.value));
    }

    void ResumeController::ListArtifacts(
        const jobpilot::core::HttpRequest &request,
        jobpilot::core::HttpResponse &response)
    {
        const std::optional<std::uint64_t> userId =
            AuthenticateRequest(
                request,
                &response,
                &service_);

        if (!userId.has_value())
        {
            return;
        }

        if (artifactService_ == nullptr)
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                ErrorCode::kInternalError,
                nullptr);
            return;
        }

        const ServiceResult<
            std::vector<ResumeArtifactRecord>> result =
                artifactService_->List(userId.value());

        if (result.code != ErrorCode::kSuccess)
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                result.code,
                nullptr);
            return;
        }

        Json list = Json::array();

        for (const ResumeArtifactRecord &artifact :
             result.value)
        {
            list.push_back(ArtifactToJson(artifact));
        }

        WriteResponse(
            &response,
            request.KeepAlive(),
            ErrorCode::kSuccess,
            Json{{"list", std::move(list)}});
    }

    void ResumeController::GetArtifact(
        const jobpilot::core::HttpRequest &request,
        jobpilot::core::HttpResponse &response)
    {
        const std::optional<std::uint64_t> userId =
            AuthenticateRequest(
                request,
                &response,
                &service_);

        if (!userId.has_value())
        {
            return;
        }

        const std::optional<std::uint64_t> artifactId =
            ParseArtifactId(request.Path(), "");

        if (artifactService_ == nullptr)
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                ErrorCode::kInternalError,
                nullptr);
            return;
        }

        if (!artifactId.has_value())
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                ErrorCode::kInvalidParam,
                nullptr);
            return;
        }

        const ServiceResult<ResumeArtifactRecord> result =
            artifactService_->Get(
                userId.value(),
                artifactId.value());

        if (result.code != ErrorCode::kSuccess)
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                result.code,
                nullptr);
            return;
        }

        WriteResponse(
            &response,
            request.KeepAlive(),
            ErrorCode::kSuccess,
            ArtifactToJson(result.value));
    }

    void ResumeController::ConfirmArtifact(
        const jobpilot::core::HttpRequest &request,
        jobpilot::core::HttpResponse &response)
    {
        const std::optional<std::uint64_t> userId =
            AuthenticateRequest(
                request,
                &response,
                &service_);

        if (!userId.has_value())
        {
            return;
        }

        const std::optional<std::uint64_t> artifactId =
            ParseArtifactId(request.Path(), "/confirm");

        if (artifactService_ == nullptr)
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                ErrorCode::kInternalError,
                nullptr);
            return;
        }

        if (!artifactId.has_value())
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                ErrorCode::kInvalidParam,
                nullptr);
            return;
        }

        const ServiceResult<ResumeArtifactRecord> result =
            artifactService_->Confirm(
                userId.value(),
                artifactId.value());

        if (result.code != ErrorCode::kSuccess)
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                result.code,
                nullptr);
            return;
        }

        WriteResponse(
            &response,
            request.KeepAlive(),
            ErrorCode::kSuccess,
            ArtifactToJson(result.value));
    }

} // namespace jobpilot::service
