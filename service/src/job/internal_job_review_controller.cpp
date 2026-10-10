#include "jobpilot/service/job/internal_job_review_controller.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

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
            // A second, conflicting decision on an already-resolved row.
            case ErrorCode::kInvalidStatusTransition:
                return 409;
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
            case 409:
                return "Conflict";
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
            ErrorCode code,
            const Json &data = Json(nullptr))
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
                    {"code", static_cast<int>(code)},
                    {"message", ErrorMessage(code)},
                    {"data", data}}
                    .dump());
            response->SetKeepAlive(request.KeepAlive());
        }

        bool ConstantTimeEquals(
            std::string_view expected,
            std::string_view actual)
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

        bool GetOptionalString(
            const Json &object,
            const char *key,
            std::optional<std::string> *value)
        {
            const auto iterator = object.find(key);

            if (iterator == object.end() ||
                iterator->is_null())
            {
                value->reset();
                return true;
            }

            if (!iterator->is_string())
            {
                return false;
            }

            *value = iterator->get<std::string>();
            return true;
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

                const int high = HexValue(value[index + 1]);
                const int low = HexValue(value[index + 2]);

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
                        path.substr(position, end - position);
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

        // /internal/jobs/review/<id>
        std::optional<std::uint64_t> ParseReviewJobId(
            const std::string &path)
        {
            constexpr const char *kPrefix =
                "/internal/jobs/review/";

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

        std::optional<std::size_t> ParsePositiveSize(
            const std::optional<std::string> &value)
        {
            if (!value.has_value() || value->empty())
            {
                return std::nullopt;
            }

            std::size_t parsed = 0;

            try
            {
                const unsigned long long number =
                    std::stoull(value.value(), &parsed);

                if (parsed != value->size() || number == 0)
                {
                    return std::nullopt;
                }

                return static_cast<std::size_t>(number);
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

        Json QueueItemToJson(
            const JobReviewQueueItem &item)
        {
            return Json{
                {"job_id", item.jobId},
                {"title", item.title},
                {"company", item.company},
                {"location", NullableString(item.location)},
                {"source_url", NullableString(item.sourceUrl)},
                {"source", item.source},
                {"description_excerpt",
                 NullableString(item.descriptionExcerpt)},
                {"review_status", item.reviewStatus},
                {"reason_code", item.reasonCode},
                {"reason_text", item.reasonText},
                {"outcome", item.outcome},
                {"resolution", item.resolution},
                {"created_at", item.createTime}};
        }
    } // namespace

    InternalJobReviewController::InternalJobReviewController(
        JobReviewService &service,
        std::string internalToken)
        : service_(service),
          internalToken_(std::move(internalToken))
    {
    }

    void InternalJobReviewController::Queue(
        const jobpilot::core::HttpRequest &request,
        jobpilot::core::HttpResponse &response)
    {
        if (!IsAuthorized(request, &response, internalToken_))
        {
            return;
        }

        const std::optional<std::string> status =
            ParseQueryValue(request.Path(), "status");

        if (status.has_value() &&
            *status != "needs_review" &&
            *status != "quarantined" &&
            *status != "clean")
        {
            WriteError(
                request,
                &response,
                ErrorCode::kInvalidParam);
            return;
        }

        std::size_t page = 1;
        std::size_t pageSize = 20;

        if (const std::optional<std::string> value =
                ParseQueryValue(request.Path(), "page"))
        {
            const std::optional<std::size_t> parsed =
                ParsePositiveSize(value);

            if (!parsed.has_value())
            {
                WriteError(
                    request,
                    &response,
                    ErrorCode::kInvalidParam);
                return;
            }

            page = parsed.value();
        }

        if (const std::optional<std::string> value =
                ParseQueryValue(request.Path(), "page_size"))
        {
            const std::optional<std::size_t> parsed =
                ParsePositiveSize(value);

            if (!parsed.has_value())
            {
                WriteError(
                    request,
                    &response,
                    ErrorCode::kInvalidParam);
                return;
            }

            pageSize = parsed.value();
        }

        const ServiceResult<JobReviewQueuePage> result =
            service_.ListQueue(
                status.value_or(std::string()),
                page,
                pageSize);

        if (result.code != ErrorCode::kSuccess)
        {
            WriteError(request, &response, result.code);
            return;
        }

        Json items = Json::array();

        for (const JobReviewQueueItem &item :
             result.value.items)
        {
            items.push_back(QueueItemToJson(item));
        }

        WriteJson(
            request,
            &response,
            Json{
                {"items", std::move(items)},
                {"total", result.value.total},
                {"page", result.value.page},
                {"page_size", result.value.pageSize}});
    }

    void InternalJobReviewController::Review(
        const jobpilot::core::HttpRequest &request,
        jobpilot::core::HttpResponse &response)
    {
        if (!IsAuthorized(request, &response, internalToken_))
        {
            return;
        }

        const std::optional<std::uint64_t> jobId =
            ParseReviewJobId(request.Path());

        if (!jobId.has_value())
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

        std::string decision;
        std::string reviewer;
        std::string note;
        JobReviewFix fix;

        const auto decisionValue = body.find("decision");

        if (decisionValue == body.end() ||
            !decisionValue->is_string())
        {
            WriteError(
                request,
                &response,
                ErrorCode::kInvalidParam);
            return;
        }

        decision = decisionValue->get<std::string>();

        std::optional<std::string> reviewerValue;
        std::optional<std::string> noteValue;

        if (!GetOptionalString(body, "reviewer", &reviewerValue) ||
            !GetOptionalString(body, "note", &noteValue))
        {
            WriteError(
                request,
                &response,
                ErrorCode::kInvalidParam);
            return;
        }

        reviewer = reviewerValue.value_or(std::string());
        note = noteValue.value_or(std::string());

        const auto fixValue = body.find("fix");

        if (fixValue != body.end() && !fixValue->is_null())
        {
            if (!fixValue->is_object())
            {
                WriteError(
                    request,
                    &response,
                    ErrorCode::kInvalidParam);
                return;
            }

            if (!GetOptionalString(*fixValue, "company", &fix.company) ||
                !GetOptionalString(*fixValue, "title", &fix.title) ||
                !GetOptionalString(
                    *fixValue,
                    "source_url",
                    &fix.sourceUrl) ||
                !GetOptionalString(
                    *fixValue,
                    "description",
                    &fix.description))
            {
                WriteError(
                    request,
                    &response,
                    ErrorCode::kInvalidParam);
                return;
            }
        }

        const ServiceResult<JobReviewResult> result =
            service_.Review(
                jobId.value(),
                decision,
                reviewer,
                note,
                fix);

        if (result.code != ErrorCode::kSuccess)
        {
            Json data = Json{
                {"job_id", result.value.jobId},
                {"decision", result.value.decision},
                {"idempotent", result.value.idempotent}};

            if (!result.value.diagnosticCode.empty())
            {
                data["diagnostic"] = Json{
                    {"code", result.value.diagnosticCode},
                    {"field", result.value.diagnosticField},
                    {"reason", result.value.diagnosticReason}};
            }

            WriteError(request, &response, result.code, data);
            return;
        }

        WriteJson(
            request,
            &response,
            Json{
                {"job_id", result.value.jobId},
                {"review_status", result.value.reviewStatus},
                {"decision", result.value.decision},
                {"reviewer", result.value.reviewer},
                {"note", result.value.note},
                {"idempotent", result.value.idempotent},
                {"changed_fields", result.value.changedFields}});
    }

    void InternalJobReviewController::Backfill(
        const jobpilot::core::HttpRequest &request,
        jobpilot::core::HttpResponse &response)
    {
        if (!IsAuthorized(request, &response, internalToken_))
        {
            return;
        }

        const std::optional<std::string> mode =
            ParseQueryValue(request.Path(), "mode");

        if (!mode.has_value() ||
            (*mode != "dry_run" &&
             *mode != "apply" &&
             *mode != "revert"))
        {
            WriteError(
                request,
                &response,
                ErrorCode::kInvalidParam);
            return;
        }

        const ServiceResult<JobReviewBackfillReport> result =
            service_.Backfill(*mode, std::string());

        if (result.code != ErrorCode::kSuccess)
        {
            WriteError(request, &response, result.code);
            return;
        }

        Json perReason = Json::object();

        for (const auto &entry : result.value.perReason)
        {
            perReason[entry.first] = entry.second;
        }

        Json sample = Json::array();

        for (const JobReviewQueueItem &item :
             result.value.sample)
        {
            sample.push_back(QueueItemToJson(item));
        }

        WriteJson(
            request,
            &response,
            Json{
                {"mode", result.value.mode},
                {"active_jobs", result.value.activeJobs},
                {"scanned", result.value.scanned},
                {"would_flag", result.value.wouldFlag},
                {"hidden_share_percent",
                 result.value.hiddenSharePercent},
                {"threshold_percent",
                 result.value.thresholdPercent},
                {"threshold_rows", result.value.thresholdRows},
                {"threshold_exceeded",
                 result.value.thresholdExceeded},
                {"per_reason", std::move(perReason)},
                {"sample", std::move(sample)},
                {"before_needs_review",
                 result.value.beforeNeedsReview},
                {"after_needs_review",
                 result.value.afterNeedsReview},
                {"applied", result.value.applied},
                {"reverted", result.value.reverted}});
    }

} // namespace jobpilot::service
