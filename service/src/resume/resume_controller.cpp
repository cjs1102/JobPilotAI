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
                return 404;
            case ErrorCode::kEmailExists:
                return 409;
            case ErrorCode::kUserNotFound:
            case ErrorCode::kInvalidPassword:
                return 401;
            case ErrorCode::kInternalError:
            case ErrorCode::kDatabaseError:
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
                    nullptr);

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
                            nullptr);

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
                            nullptr);

                        return std::nullopt;
                    }

                    file = &part;
                }
            }

            if (title == nullptr ||
                file == nullptr ||
                !file->filename.has_value())
            {
                WriteResponse(
                    response,
                    request.KeepAlive(),
                    ErrorCode::kInvalidParam,
                    nullptr);

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
        ResumeService &service)
        : service_(service)
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
                {"version", result.value.version}});
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
                nullptr);

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
                {"version", result.value.version}});
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
            list.push_back(
                Json{
                    {"id", version.id},
                    {"version", version.version},
                    {"created_at", version.createTime}});
        }

        WriteResponse(
            &response,
            request.KeepAlive(),
            ErrorCode::kSuccess,
            Json{{"list", std::move(list)}});
    }

} // namespace jobpilot::service
