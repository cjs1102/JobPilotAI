#include "jobpilot/service/ai/ai_controller.h"

#include <cstdint>
#include <exception>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "jobpilot/error.h"
#include "jobpilot/service/auth/session_authenticator.h"

namespace jobpilot::service
{
    namespace
    {
        using Json = nlohmann::json;

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
            case ErrorCode::kAiTaskNotFound:
                return 404;
            case ErrorCode::kEmailExists:
            case ErrorCode::kDuplicateApplication:
                return 409;
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
            ISessionAuthenticator *authenticator)
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
                authenticator->Authenticate(token.value());

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
                *body = Json::parse(request.Body());
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

        bool ReadPositiveUnsigned(
            const Json &object,
            const char *key,
            std::uint64_t *value)
        {
            const auto iterator =
                object.find(key);

            if (iterator == object.end() ||
                !iterator->is_number_integer())
            {
                return false;
            }

            if (iterator->is_number_unsigned())
            {
                const std::uint64_t parsed =
                    iterator->get<std::uint64_t>();

                if (parsed == 0)
                {
                    return false;
                }

                *value = parsed;
                return true;
            }

            const std::int64_t parsed =
                iterator->get<std::int64_t>();

            if (parsed <= 0)
            {
                return false;
            }

            *value =
                static_cast<std::uint64_t>(parsed);
            return true;
        }

        std::optional<std::uint64_t> ParseTaskId(
            const std::string &path)
        {
            constexpr const char *kPrefix =
                "/api/v1/ai/tasks/";

            const std::size_t queryPosition =
                path.find('?');
            const std::string pathOnly =
                path.substr(0, queryPosition);

            if (pathOnly.rfind(kPrefix, 0) != 0)
            {
                return std::nullopt;
            }

            const std::string text =
                pathOnly.substr(
                    std::string(kPrefix).size());

            if (text.empty() ||
                text.find('/') != std::string::npos)
            {
                return std::nullopt;
            }

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

        // rule_result/result are stored as raw JSON; the contract exposes them
        // as JSON objects and null when absent (ExecPlan 0013 section 5.2).
        Json RawJson(
            const std::optional<std::string> &value)
        {
            if (!value.has_value() ||
                value->empty())
            {
                return Json(nullptr);
            }

            try
            {
                return Json::parse(value.value());
            }
            catch (const Json::parse_error &)
            {
                return Json(nullptr);
            }
        }

        Json NullableString(
            const std::optional<std::string> &value)
        {
            return value.has_value()
                       ? Json(value.value())
                       : Json(nullptr);
        }

        Json TaskToJson(
            const AiTaskView &view)
        {
            return Json{
                {"task_id", view.taskId},
                {"task_type", view.taskType},
                {"status", view.status},
                {"rule_result", RawJson(view.ruleResult)},
                {"result", RawJson(view.result)},
                {"error_msg", NullableString(view.errorMsg)},
                {"created_at", view.createdAt},
                {"updated_at", view.updatedAt}};
        }
    } // namespace

    AiController::AiController(
        AiTaskService &service,
        ISessionAuthenticator &authenticator)
        : service_(service),
          authenticator_(authenticator)
    {
    }

    void AiController::CreateMatch(
        const jobpilot::core::HttpRequest &request,
        jobpilot::core::HttpResponse &response)
    {
        const std::optional<std::uint64_t> userId =
            AuthenticateRequest(
                request,
                &response,
                &authenticator_);

        if (!userId.has_value())
        {
            return;
        }

        Json body;

        if (!ParseJsonBody(request, &response, &body))
        {
            return;
        }

        std::uint64_t jobId = 0;
        std::uint64_t resumeId = 0;

        if (!ReadPositiveUnsigned(body, "job_id", &jobId) ||
            !ReadPositiveUnsigned(body, "resume_id", &resumeId))
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                ErrorCode::kInvalidParam,
                nullptr);
            return;
        }

        const ServiceResult<AiTaskView> result =
            service_.CreateJobMatch(
                userId.value(),
                jobId,
                resumeId);

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
            Json{{"task_id", result.value.taskId}});
    }

    void AiController::GetTask(
        const jobpilot::core::HttpRequest &request,
        jobpilot::core::HttpResponse &response)
    {
        const std::optional<std::uint64_t> userId =
            AuthenticateRequest(
                request,
                &response,
                &authenticator_);

        if (!userId.has_value())
        {
            return;
        }

        const std::optional<std::uint64_t> taskId =
            ParseTaskId(request.Path());

        if (!taskId.has_value())
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                ErrorCode::kInvalidParam,
                nullptr);
            return;
        }

        const ServiceResult<AiTaskView> result =
            service_.GetOwnedTask(
                userId.value(),
                taskId.value());

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
            TaskToJson(result.value));
    }

} // namespace jobpilot::service
