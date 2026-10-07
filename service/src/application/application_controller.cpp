#include "jobpilot/service/application/application_controller.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "jobpilot/error.h"

namespace jobpilot::service
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::size_t kDefaultPage = 1;
        constexpr std::size_t kDefaultPageSize = 20;

        struct ListQuery
        {
            std::optional<std::int32_t> status;
            std::size_t page = kDefaultPage;
            std::size_t pageSize = kDefaultPageSize;
        };

        int HttpStatusFor(ErrorCode code)
        {
            switch (code)
            {
            case ErrorCode::kSuccess:
                return 200;
            case ErrorCode::kInvalidParam:
            case ErrorCode::kMalformedRequest:
            case ErrorCode::kInvalidStatusTransition:
                return 400;
            case ErrorCode::kUnauthorized:
            case ErrorCode::kTokenExpired:
            case ErrorCode::kUserNotFound:
            case ErrorCode::kInvalidPassword:
                return 401;
            case ErrorCode::kForbidden:
                return 403;
            case ErrorCode::kJobNotFound:
            case ErrorCode::kResumeNotFound:
            case ErrorCode::kApplicationNotFound:
                return 404;
            case ErrorCode::kDuplicateApplication:
            case ErrorCode::kEmailExists:
                return 409;
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
            ApplicationService *service)
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

        bool TryGetUint64(
            const Json &body,
            const char *key,
            std::uint64_t *value)
        {
            const auto iterator =
                body.find(key);

            if (iterator == body.end() ||
                !iterator->is_number_unsigned())
            {
                return false;
            }

            const std::uint64_t parsed =
                iterator->get<std::uint64_t>();

            if (parsed == 0)
            {
                return false;
            }

            *value = parsed;
            return true;
        }

        bool TryGetInt32(
            const Json &body,
            const char *key,
            std::int32_t *value)
        {
            const auto iterator =
                body.find(key);

            if (iterator == body.end() ||
                !iterator->is_number_integer())
            {
                return false;
            }

            const std::int64_t parsed =
                iterator->get<std::int64_t>();

            if (parsed <
                    std::numeric_limits<
                        std::int32_t>::min() ||
                parsed >
                    std::numeric_limits<
                        std::int32_t>::max())
            {
                return false;
            }

            *value =
                static_cast<std::int32_t>(parsed);
            return true;
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

        bool ParsePositiveSize(
            const std::string &value,
            std::size_t *result)
        {
            if (value.empty())
            {
                return false;
            }

            std::size_t parsed = 0;

            try
            {
                const unsigned long long number =
                    std::stoull(value, &parsed);

                if (parsed != value.size() ||
                    number >
                        std::numeric_limits<
                            std::size_t>::max())
                {
                    return false;
                }

                *result =
                    static_cast<std::size_t>(number);
                return true;
            }
            catch (const std::exception &)
            {
                return false;
            }
        }

        bool ParseListQuery(
            const std::string &path,
            ListQuery *query)
        {
            const std::size_t queryPosition =
                path.find('?');

            if (queryPosition == std::string::npos)
            {
                return true;
            }

            bool hasStatus = false;
            bool hasPage = false;
            bool hasPageSize = false;
            std::size_t position = queryPosition + 1;

            while (position <= path.size())
            {
                const std::size_t ampersand =
                    path.find('&', position);
                const std::size_t end =
                    ampersand == std::string::npos
                        ? path.size()
                        : ampersand;

                if (end > position)
                {
                    const std::string field =
                        path.substr(
                            position,
                            end - position);
                    const std::size_t equals =
                        field.find('=');

                    if (equals == std::string::npos)
                    {
                        return false;
                    }

                    const std::string key =
                        field.substr(0, equals);
                    const std::string value =
                        field.substr(equals + 1);

                    if (key == "status")
                    {
                        std::size_t parsed = 0;

                        if (hasStatus ||
                            value.empty())
                        {
                            return false;
                        }

                        try
                        {
                            const long number =
                                std::stol(value, &parsed);

                            if (parsed != value.size() ||
                                number < 0 ||
                                number > 7)
                            {
                                return false;
                            }

                            query->status =
                                static_cast<std::int32_t>(
                                    number);
                        }
                        catch (const std::exception &)
                        {
                            return false;
                        }

                        hasStatus = true;
                    }
                    else if (key == "page")
                    {
                        if (hasPage ||
                            !ParsePositiveSize(
                                value,
                                &query->page))
                        {
                            return false;
                        }

                        hasPage = true;
                    }
                    else if (key == "page_size")
                    {
                        if (hasPageSize ||
                            !ParsePositiveSize(
                                value,
                                &query->pageSize))
                        {
                            return false;
                        }

                        hasPageSize = true;
                    }
                    else
                    {
                        return false;
                    }
                }

                if (ampersand == std::string::npos)
                {
                    break;
                }

                position = ampersand + 1;
            }

            return query->page >= 1 &&
                   query->pageSize >= 1 &&
                   query->pageSize <= 100;
        }

        std::optional<std::uint64_t> ParseApplicationId(
            const std::string &path)
        {
            constexpr const char *kPrefix =
                "/api/v1/applications/";
            constexpr const char *kSuffix =
                "/status";

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

        Json NullableString(
            const std::optional<std::string> &value)
        {
            return value.has_value()
                       ? Json(value.value())
                       : Json(nullptr);
        }
    } // namespace

    ApplicationController::ApplicationController(
        ApplicationService &service)
        : service_(service)
    {
    }

    void ApplicationController::Create(
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

        Json body;

        if (!ParseJsonBody(
                request,
                &response,
                &body))
        {
            return;
        }

        std::uint64_t jobId = 0;
        std::uint64_t resumeVersionId = 0;

        if (!TryGetUint64(body, "job_id", &jobId) ||
            !TryGetUint64(
                body,
                "resume_version_id",
                &resumeVersionId))
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                ErrorCode::kInvalidParam,
                nullptr);
            return;
        }

        const ServiceResult<ApplicationCreateData> result =
            service_.Create(
                userId.value(),
                jobId,
                resumeVersionId);

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
                {"application_id",
                 result.value.applicationId},
                {"status", result.value.status}});
    }

    void ApplicationController::List(
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

        ListQuery query;

        if (!ParseListQuery(
                request.Path(),
                &query))
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                ErrorCode::kInvalidParam,
                nullptr);
            return;
        }

        const ServiceResult<ApplicationPage> result =
            service_.List(
                userId.value(),
                query.status,
                query.page,
                query.pageSize);

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

        for (const ApplicationListItem &item :
             result.value.list)
        {
            list.push_back(
                Json{
                    {"id", item.id},
                    {"job_id", item.jobId},
                    {"job_title", item.jobTitle},
                    {"company", item.company},
                    {"status", item.status},
                    {"apply_time",
                     NullableString(item.applyTime)},
                    {"remark",
                     NullableString(item.remark)}});
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

    void ApplicationController::UpdateStatus(
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

        const std::optional<std::uint64_t> applicationId =
            ParseApplicationId(request.Path());

        if (!applicationId.has_value())
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

        std::int32_t status = 0;

        if (!TryGetInt32(body, "status", &status))
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                ErrorCode::kInvalidParam,
                nullptr);
            return;
        }

        const ServiceResult<std::int32_t> result =
            service_.UpdateStatus(
                userId.value(),
                applicationId.value(),
                status);

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
            Json{{"status", result.value}});
    }

    void ApplicationController::Statistics(
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

        const ServiceResult<ApplicationStatistics> result =
            service_.Statistics(userId.value());

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
                {"total", result.value.total},
                {"interview", result.value.interview},
                {"offer", result.value.offer},
                {"rejected", result.value.rejected}});
    }

} // namespace jobpilot::service
