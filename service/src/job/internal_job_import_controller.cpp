#include "jobpilot/service/job/internal_job_import_controller.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "jobpilot/error.h"
#include "jobpilot/service/job/job_extra_fields.h"

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
                return 401;
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
            default:
                return "Internal Server Error";
            }
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
            response->SetKeepAlive(
                request.KeepAlive());
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

        bool GetOptionalString(
            const Json &object,
            const char *key,
            std::optional<std::string> *value)
        {
            const auto iterator =
                object.find(key);

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

        bool GetOptionalUnsigned(
            const Json &object,
            const char *key,
            std::optional<std::uint32_t> *value)
        {
            const auto iterator =
                object.find(key);

            if (iterator == object.end() ||
                iterator->is_null())
            {
                value->reset();
                return true;
            }

            if (!iterator->is_number_unsigned())
            {
                return false;
            }

            const std::uint64_t parsed =
                iterator->get<std::uint64_t>();

            if (parsed >
                std::numeric_limits<
                    std::uint32_t>::max())
            {
                return false;
            }

            *value =
                static_cast<std::uint32_t>(
                    parsed);
            return true;
        }

        bool GetOptionalBool(
            const Json &object,
            const char *key,
            std::optional<bool> *value)
        {
            const auto iterator =
                object.find(key);

            if (iterator == object.end() ||
                iterator->is_null())
            {
                value->reset();
                return true;
            }

            if (!iterator->is_boolean())
            {
                return false;
            }

            *value = iterator->get<bool>();
            return true;
        }

        bool ParseRequirements(
            const Json &object,
            std::vector<JobRequirementImportData> *requirements)
        {
            const auto iterator =
                object.find("requirements");

            if (iterator == object.end() ||
                iterator->is_null())
            {
                requirements->clear();
                return true;
            }

            if (!iterator->is_array())
            {
                return false;
            }

            requirements->clear();
            requirements->reserve(iterator->size());

            for (const Json &value : *iterator)
            {
                if (!value.is_object())
                {
                    return false;
                }

                JobRequirementImportData requirement;

                if (!GetRequiredString(
                        value,
                        "skill",
                        &requirement.skill))
                {
                    return false;
                }

                const auto level =
                    value.find("level");

                if (level == value.end() ||
                    !level->is_number_integer())
                {
                    return false;
                }

                const std::int64_t parsed =
                    level->get<std::int64_t>();

                if (parsed <
                        std::numeric_limits<
                            std::int32_t>::min() ||
                    parsed >
                        std::numeric_limits<
                            std::int32_t>::max())
                {
                    return false;
                }

                requirement.level =
                    static_cast<std::int32_t>(
                        parsed);
                requirements->push_back(
                    std::move(requirement));
            }

            return true;
        }

        bool ParseJob(
            const Json &value,
            JobIngestionItem *item)
        {
            if (!value.is_object())
            {
                return false;
            }

            JobImportData &job = item->job;

            if (!GetRequiredString(
                    value,
                    "company",
                    &job.company) ||
                !GetRequiredString(
                    value,
                    "title",
                    &job.title) ||
                !GetRequiredString(
                    value,
                    "source",
                    &job.source) ||
                !GetRequiredString(
                    value,
                    "source_url",
                    &job.sourceUrl) ||
                !GetOptionalString(
                    value,
                    "location",
                    &job.location) ||
                !GetOptionalString(
                    value,
                    "description",
                    &job.description) ||
                !GetOptionalUnsigned(
                    value,
                    "salary_min",
                    &job.salaryMin) ||
                !GetOptionalUnsigned(
                    value,
                    "salary_max",
                    &job.salaryMax) ||
                !GetOptionalUnsigned(
                    value,
                    "education_required",
                    &job.educationRequired) ||
                !GetOptionalUnsigned(
                    value,
                    "experience_years_min",
                    &job.experienceYearsMin) ||
                !GetOptionalUnsigned(
                    value,
                    "experience_years_max",
                    &job.experienceYearsMax) ||
                !GetOptionalUnsigned(
                    value,
                    "job_nature",
                    &job.jobNature) ||
                !GetOptionalString(
                    value,
                    "deadline",
                    &job.deadline) ||
                !GetOptionalBool(
                    value,
                    "deadline_date_only",
                    &job.deadlineDateOnly))
            {
                return false;
            }

            const auto currency =
                value.find("salary_currency");

            if (currency != value.end() &&
                !currency->is_null())
            {
                if (!currency->is_string())
                {
                    return false;
                }

                job.salaryCurrency =
                    currency->get<std::string>();
            }

            NormalizeJobExtraFields(&job);

            return ParseRequirements(
                value,
                &item->requirements);
        }
    } // namespace

    InternalJobImportController::InternalJobImportController(
        JobIngestionService &service,
        std::string internalToken)
        : service_(service),
          internalToken_(std::move(internalToken))
    {
    }

    void InternalJobImportController::Import(
        const jobpilot::core::HttpRequest &request,
        jobpilot::core::HttpResponse &response)
    {
        const std::string providedToken =
            request.GetHeader("X-Internal-Token");

        if (internalToken_.empty() ||
            !ConstantTimeEquals(
                internalToken_,
                providedToken))
        {
            WriteError(
                request,
                &response,
                ErrorCode::kUnauthorized);
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

        const auto jobs =
            body.find("jobs");

        if (jobs == body.end() ||
            !jobs->is_array())
        {
            WriteError(
                request,
                &response,
                ErrorCode::kInvalidParam);
            return;
        }

        std::vector<JobIngestionItem> items;
        items.reserve(jobs->size());

        for (const Json &job : *jobs)
        {
            JobIngestionItem item;

            if (!ParseJob(job, &item))
            {
                WriteError(
                    request,
                    &response,
                    ErrorCode::kInvalidParam);
                return;
            }

            items.push_back(
                std::move(item));
        }

        const ServiceResult<JobIngestionResult> result =
            service_.Import(items);

        if (result.code != ErrorCode::kSuccess)
        {
            WriteError(
                request,
                &response,
                result.code);
            return;
        }

        response.SetStatusCode(200);
        response.SetStatusMessage("OK");
        response.SetHeader(
            "Content-Type",
            "application/json");
        response.SetBody(
            Json{
                {"created", result.value.created},
                {"updated", result.value.updated},
                {"total", result.value.total}}
                .dump());
        response.SetKeepAlive(
            request.KeepAlive());
    }

} // namespace jobpilot::service
