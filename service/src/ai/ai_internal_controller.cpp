#include "jobpilot/service/ai/ai_internal_controller.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "jobpilot/error.h"

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
                return 400;
            case ErrorCode::kUnauthorized:
            case ErrorCode::kTokenExpired:
                return 401;
            case ErrorCode::kJobNotFound:
            case ErrorCode::kResumeNotFound:
            case ErrorCode::kApplicationNotFound:
            case ErrorCode::kAiTaskNotFound:
                return 404;
            case ErrorCode::kAiServiceBusy:
                return 503;
            case ErrorCode::kAiAnalysisFailed:
            case ErrorCode::kInternalError:
            case ErrorCode::kDatabaseError:
                return 500;
            default:
                return 500;
            }
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
            case 404:
                return "Not Found";
            case 503:
                return "Service Unavailable";
            default:
                return "Internal Server Error";
            }
        }

        void WriteJson(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse *response,
            const Json &body)
        {
            response->SetStatusCode(200);
            response->SetStatusMessage("OK");
            response->SetHeader(
                "Content-Type",
                "application/json");
            response->SetBody(body.dump());
            response->SetKeepAlive(request.KeepAlive());
        }

        void WriteError(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse *response,
            ErrorCode code)
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
                    {"data", nullptr}}
                    .dump());
            response->SetKeepAlive(request.KeepAlive());
        }

        bool ConstantTimeEquals(
            const std::string &expected,
            const std::string &actual)
        {
            if (expected.size() != actual.size())
            {
                return false;
            }

            unsigned char difference = 0;

            for (std::size_t index = 0;
                 index < expected.size();
                 ++index)
            {
                difference |=
                    static_cast<unsigned char>(
                        expected[index] ^ actual[index]);
            }

            return difference == 0;
        }

        bool IsAuthorized(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse *response,
            const std::string &internalToken)
        {
            const std::string provided =
                request.GetHeader("X-Internal-Token");

            if (internalToken.empty() ||
                !ConstantTimeEquals(internalToken, provided))
            {
                WriteError(
                    request,
                    response,
                    ErrorCode::kUnauthorized);
                return false;
            }

            return true;
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
                WriteError(
                    request,
                    response,
                    ErrorCode::kMalformedRequest);
                return false;
            }

            if (!body->is_object())
            {
                WriteError(
                    request,
                    response,
                    ErrorCode::kInvalidParam);
                return false;
            }

            return true;
        }

        bool GetRequiredString(
            const Json &object,
            const char *key,
            std::string *value)
        {
            const auto iterator =
                object.find(key);

            if (iterator == object.end() ||
                !iterator->is_string())
            {
                return false;
            }

            *value = iterator->get<std::string>();
            return true;
        }

        bool GetRequiredBool(
            const Json &object,
            const char *key,
            bool *value)
        {
            const auto iterator =
                object.find(key);

            if (iterator == object.end() ||
                !iterator->is_boolean())
            {
                return false;
            }

            *value = iterator->get<bool>();
            return true;
        }

        // A requested lease is clamped to [1, 3600] seconds so a worker can
        // never hold a task indefinitely (0013 Step 5).
        std::int32_t ClampLeaseSeconds(
            const Json &value)
        {
            long double requested = 0.0L;

            if (value.is_number_unsigned())
            {
                requested = static_cast<long double>(
                    value.get<std::uint64_t>());
            }
            else
            {
                requested = static_cast<long double>(
                    value.get<std::int64_t>());
            }

            if (requested <
                static_cast<long double>(kMinAiLeaseSeconds))
            {
                return kMinAiLeaseSeconds;
            }

            if (requested >
                static_cast<long double>(kMaxAiLeaseSeconds))
            {
                return kMaxAiLeaseSeconds;
            }

            return static_cast<std::int32_t>(requested);
        }

        int HexValue(char character)
        {
            if (character >= '0' && character <= '9')
            {
                return character - '0';
            }

            if (character >= 'a' && character <= 'f')
            {
                return character - 'a' + 10;
            }

            if (character >= 'A' && character <= 'F')
            {
                return character - 'A' + 10;
            }

            return -1;
        }

        bool PercentDecode(
            const std::string &value,
            std::string *decoded)
        {
            decoded->clear();
            decoded->reserve(value.size());

            for (std::size_t index = 0;
                 index < value.size();
                 ++index)
            {
                if (value[index] != '%')
                {
                    decoded->push_back(value[index]);
                    continue;
                }

                if (index + 2 >= value.size())
                {
                    return false;
                }

                const int high =
                    HexValue(value[index + 1]);
                const int low =
                    HexValue(value[index + 2]);

                if (high < 0 || low < 0)
                {
                    return false;
                }

                decoded->push_back(
                    static_cast<char>((high << 4) | low));
                index += 2;
            }

            return true;
        }

        std::optional<std::string> ParseQueryValue(
            const std::string &path,
            const std::string &key)
        {
            const std::size_t queryPosition =
                path.find('?');

            if (queryPosition == std::string::npos)
            {
                return std::nullopt;
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

                if (end > position)
                {
                    const std::string field =
                        path.substr(
                            position,
                            end - position);
                    const std::size_t equals =
                        field.find('=');

                    if (equals != std::string::npos &&
                        field.substr(0, equals) == key)
                    {
                        std::string decoded;

                        if (!PercentDecode(
                                field.substr(equals + 1),
                                &decoded))
                        {
                            return std::nullopt;
                        }

                        return decoded;
                    }
                }

                if (ampersand == std::string::npos)
                {
                    break;
                }

                position = ampersand + 1;
            }

            return std::nullopt;
        }

        // Extracts the numeric id from /internal/ai/tasks/<id><suffix>.
        std::optional<std::uint64_t> ParseInternalTaskId(
            const std::string &path,
            const std::string &suffix)
        {
            constexpr const char *kPrefix =
                "/internal/ai/tasks/";

            const std::size_t queryPosition =
                path.find('?');
            const std::string pathOnly =
                path.substr(0, queryPosition);

            if (pathOnly.rfind(kPrefix, 0) != 0)
            {
                return std::nullopt;
            }

            const std::string rest =
                pathOnly.substr(
                    std::string(kPrefix).size());

            if (rest.size() <= suffix.size() ||
                rest.compare(
                    rest.size() - suffix.size(),
                    suffix.size(),
                    suffix) != 0)
            {
                return std::nullopt;
            }

            const std::string text =
                rest.substr(
                    0,
                    rest.size() - suffix.size());

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

        Json NullableString(
            const std::optional<std::string> &value)
        {
            return value.has_value()
                       ? Json(value.value())
                       : Json(nullptr);
        }

        Json NullableUnsigned(
            const std::optional<std::uint32_t> &value)
        {
            return value.has_value()
                       ? Json(value.value())
                       : Json(nullptr);
        }

        Json RawJson(
            const std::optional<std::string> &value)
        {
            if (!value.has_value() || value->empty())
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

        Json NullableUnsigned64(
            const std::optional<std::uint64_t> &value)
        {
            return value.has_value()
                       ? Json(value.value())
                       : Json(nullptr);
        }

        Json ClaimToJson(
            const AiTaskClaim &claim)
        {
            return Json{
                {"id", claim.taskId},
                {"task_type", claim.taskType},
                {"job_id", NullableUnsigned64(claim.jobId)},
                {"resume_id", NullableUnsigned64(claim.resumeId)},
                {"retry_count", claim.retryCount}};
        }

        // Input bundle exactly as ExecPlan 0013 section 5.4.
        Json InputToJson(
            const AiTaskInput &input)
        {
            Json requirements = Json::array();

            for (const JobRequirement &requirement :
                 input.job.requirements)
            {
                requirements.push_back(
                    Json{
                        {"skill", requirement.skill},
                        {"level", requirement.level}});
            }

            Json skills = Json::array();

            for (const UserSkill &skill : input.skills)
            {
                skills.push_back(
                    Json{
                        {"name", skill.name},
                        {"level", skill.level}});
            }

            const JobSummary &summary =
                input.job.summary;

            Json profile = nullptr;

            if (input.profile.has_value())
            {
                profile = Json{
                    {"target_position",
                     NullableString(
                         input.profile->targetPosition)},
                    {"location",
                     NullableString(
                         input.profile->location)},
                    {"salary_min",
                     NullableUnsigned(
                         input.profile->salaryMin)},
                    {"salary_max",
                     NullableUnsigned(
                         input.profile->salaryMax)},
                    {"summary",
                     NullableString(
                         input.profile->summary)}};
            }

            return Json{
                {"task",
                 Json{
                     {"id", input.task.taskId},
                     {"task_type", input.task.taskType},
                     {"retry_count", input.task.retryCount}}},
                {"job",
                 Json{
                     {"id", summary.id},
                     {"company", summary.company},
                     {"title", summary.title},
                     {"location",
                      NullableString(summary.location)},
                     {"salary_min",
                      NullableUnsigned(summary.salaryMin)},
                     {"salary_max",
                      NullableUnsigned(summary.salaryMax)},
                     {"salary_currency",
                      summary.salaryCurrency},
                     {"description",
                      NullableString(input.job.description)},
                     {"requirements",
                      std::move(requirements)}}},
                {"profile", std::move(profile)},
                {"skills", std::move(skills)},
                {"resume",
                 Json{
                     {"resume_id", input.resume.resumeId},
                     {"version", input.resume.version},
                     {"content", input.resume.content}}},
                {"rule_result",
                 RawJson(input.ruleResult)}};
        }
    } // namespace

    AiInternalController::AiInternalController(
        AiTaskService &service,
        std::string internalToken)
        : service_(service),
          internalToken_(std::move(internalToken))
    {
    }

    void AiInternalController::Claim(
        const jobpilot::core::HttpRequest &request,
        jobpilot::core::HttpResponse &response)
    {
        if (!IsAuthorized(request, &response, internalToken_))
        {
            return;
        }

        Json body;

        if (!ParseJsonBody(request, &response, &body))
        {
            return;
        }

        std::string workerId;

        if (!GetRequiredString(body, "worker_id", &workerId) ||
            workerId.empty())
        {
            WriteError(
                request,
                &response,
                ErrorCode::kInvalidParam);
            return;
        }

        std::optional<std::int32_t> leaseSeconds;
        const auto lease = body.find("lease_seconds");

        if (lease != body.end() && !lease->is_null())
        {
            if (!lease->is_number_integer())
            {
                WriteError(
                    request,
                    &response,
                    ErrorCode::kInvalidParam);
                return;
            }

            leaseSeconds = ClampLeaseSeconds(*lease);
        }

        const ServiceResult<AiTaskClaimResult> result =
            service_.ClaimTask(workerId, leaseSeconds);

        if (result.code != ErrorCode::kSuccess)
        {
            WriteError(request, &response, result.code);
            return;
        }

        WriteJson(
            request,
            &response,
            Json{
                {"task",
                 result.value.task.has_value()
                     ? ClaimToJson(result.value.task.value())
                     : Json(nullptr)},
                {"requeued_stale", result.value.requeuedStale},
                {"expired_failed", result.value.expiredFailed}});
    }

    void AiInternalController::Complete(
        const jobpilot::core::HttpRequest &request,
        jobpilot::core::HttpResponse &response)
    {
        if (!IsAuthorized(request, &response, internalToken_))
        {
            return;
        }

        const std::optional<std::uint64_t> taskId =
            ParseInternalTaskId(
                request.Path(),
                "/complete");

        if (!taskId.has_value())
        {
            WriteError(
                request,
                &response,
                ErrorCode::kInvalidParam);
            return;
        }

        Json body;

        if (!ParseJsonBody(request, &response, &body))
        {
            return;
        }

        std::string workerId;

        if (!GetRequiredString(body, "worker_id", &workerId) ||
            workerId.empty())
        {
            WriteError(
                request,
                &response,
                ErrorCode::kInvalidParam);
            return;
        }

        const auto result = body.find("result");

        if (result == body.end() || !result->is_object())
        {
            WriteError(
                request,
                &response,
                ErrorCode::kInvalidParam);
            return;
        }

        const ServiceResult<AiTaskView> outcome =
            service_.CompleteTask(
                workerId,
                taskId.value(),
                result->dump());

        if (outcome.code != ErrorCode::kSuccess)
        {
            WriteError(request, &response, outcome.code);
            return;
        }

        WriteJson(
            request,
            &response,
            Json{{"status", outcome.value.status}});
    }

    void AiInternalController::Fail(
        const jobpilot::core::HttpRequest &request,
        jobpilot::core::HttpResponse &response)
    {
        if (!IsAuthorized(request, &response, internalToken_))
        {
            return;
        }

        const std::optional<std::uint64_t> taskId =
            ParseInternalTaskId(
                request.Path(),
                "/fail");

        if (!taskId.has_value())
        {
            WriteError(
                request,
                &response,
                ErrorCode::kInvalidParam);
            return;
        }

        Json body;

        if (!ParseJsonBody(request, &response, &body))
        {
            return;
        }

        std::string workerId;

        if (!GetRequiredString(body, "worker_id", &workerId) ||
            workerId.empty())
        {
            WriteError(
                request,
                &response,
                ErrorCode::kInvalidParam);
            return;
        }

        std::string errorMsg;

        if (!GetRequiredString(body, "error_msg", &errorMsg))
        {
            WriteError(
                request,
                &response,
                ErrorCode::kInvalidParam);
            return;
        }

        bool retryable = false;

        if (!GetRequiredBool(body, "retryable", &retryable))
        {
            WriteError(
                request,
                &response,
                ErrorCode::kInvalidParam);
            return;
        }

        const ServiceResult<AiTaskView> outcome =
            service_.FailTask(
                workerId,
                taskId.value(),
                errorMsg,
                retryable);

        if (outcome.code != ErrorCode::kSuccess)
        {
            WriteError(request, &response, outcome.code);
            return;
        }

        WriteJson(
            request,
            &response,
            Json{{"status", outcome.value.status}});
    }

    void AiInternalController::Input(
        const jobpilot::core::HttpRequest &request,
        jobpilot::core::HttpResponse &response)
    {
        if (!IsAuthorized(request, &response, internalToken_))
        {
            return;
        }

        const std::optional<std::uint64_t> taskId =
            ParseInternalTaskId(
                request.Path(),
                "/input");

        if (!taskId.has_value())
        {
            WriteError(
                request,
                &response,
                ErrorCode::kInvalidParam);
            return;
        }

        const std::optional<std::string> workerId =
            ParseQueryValue(
                request.Path(),
                "worker_id");

        if (!workerId.has_value() ||
            workerId->empty())
        {
            WriteError(
                request,
                &response,
                ErrorCode::kInvalidParam);
            return;
        }

        const ServiceResult<AiTaskInput> result =
            service_.BuildTaskInput(
                workerId.value(),
                taskId.value());

        if (result.code != ErrorCode::kSuccess)
        {
            WriteError(request, &response, result.code);
            return;
        }

        WriteJson(
            request,
            &response,
            InputToJson(result.value));
    }

} // namespace jobpilot::service
