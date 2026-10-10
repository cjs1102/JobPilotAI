#include "jobpilot/service/job/job_controller.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "jobpilot/error.h"
#include "jobpilot/service/auth/session_authenticator.h"
#include "jobpilot/service/job/job_extra_fields.h"
#include "jobpilot/service/job/job_ranking_service.h"

namespace jobpilot::service
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::size_t kDefaultPage = 1;
        constexpr std::size_t kDefaultPageSize = 20;

        struct JobSearchQuery
        {
            JobSearchFilter filter;
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
            case ErrorCode::kJobNotFound:
            case ErrorCode::kResumeNotFound:
            case ErrorCode::kApplicationNotFound:
                return 404;
            case ErrorCode::kUnauthorized:
            case ErrorCode::kTokenExpired:
            case ErrorCode::kUserNotFound:
            case ErrorCode::kInvalidPassword:
                return 401;
            case ErrorCode::kForbidden:
                return 403;
            case ErrorCode::kEmailExists:
            case ErrorCode::kDuplicateApplication:
                return 409;
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

        int HexValue(char character)
        {
            if (character >= '0' &&
                character <= '9')
            {
                return character - '0';
            }

            if (character >= 'a' &&
                character <= 'f')
            {
                return character - 'a' + 10;
            }

            if (character >= 'A' &&
                character <= 'F')
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

            for (std::size_t i = 0;
                 i < value.size();
                 ++i)
            {
                if (value[i] != '%')
                {
                    decoded->push_back(value[i]);
                    continue;
                }

                if (i + 2 >= value.size())
                {
                    return false;
                }

                const int high =
                    HexValue(value[i + 1]);
                const int low =
                    HexValue(value[i + 2]);

                if (high < 0 || low < 0)
                {
                    return false;
                }

                decoded->push_back(
                    static_cast<char>(
                        (high << 4) | low));
                i += 2;
            }

            return true;
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

        bool ParseUnsigned32(
            const std::string &value,
            std::uint32_t *result)
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
                            std::uint32_t>::max())
                {
                    return false;
                }

                *result =
                    static_cast<std::uint32_t>(
                        number);
                return true;
            }
            catch (const std::exception &)
            {
                return false;
            }
        }

        bool ParseSearchQuery(
            const std::string &path,
            JobSearchQuery *query)
        {
            const std::size_t queryPosition =
                path.find('?');

            if (queryPosition == std::string::npos)
            {
                return true;
            }

            bool hasKeyword = false;
            bool hasLocation = false;
            bool hasSalaryMin = false;
            bool hasSalaryMax = false;
            bool hasSkill = false;
            bool hasPage = false;
            bool hasPageSize = false;
            bool hasIncludeStale = false;
            bool hasPostedWithinDays = false;
            bool hasEducationMax = false;
            bool hasJobNature = false;
            bool hasDeadlineBefore = false;
            bool hasDeadlineAfter = false;
            bool hasOpenOnly = false;
            bool hasSort = false;

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
                    std::string value;

                    if (!PercentDecode(
                            field.substr(equals + 1),
                            &value))
                    {
                        return false;
                    }

                    if (key == "keyword")
                    {
                        if (hasKeyword)
                        {
                            return false;
                        }

                        hasKeyword = true;
                        query->filter.keyword = value;
                    }
                    else if (key == "location")
                    {
                        if (hasLocation)
                        {
                            return false;
                        }

                        hasLocation = true;
                        query->filter.location = value;
                    }
                    else if (key == "salary_min")
                    {
                        std::uint32_t salary = 0;

                        if (hasSalaryMin ||
                            !ParseUnsigned32(
                                value,
                                &salary))
                        {
                            return false;
                        }

                        hasSalaryMin = true;
                        query->filter.salaryMin = salary;
                    }
                    else if (key == "salary_max")
                    {
                        std::uint32_t salary = 0;

                        if (hasSalaryMax ||
                            !ParseUnsigned32(
                                value,
                                &salary))
                        {
                            return false;
                        }

                        hasSalaryMax = true;
                        query->filter.salaryMax = salary;
                    }
                    else if (key == "skill")
                    {
                        if (hasSkill)
                        {
                            return false;
                        }

                        hasSkill = true;
                        query->filter.skill = value;
                    }
                    else if (key == "education_max")
                    {
                        std::uint32_t education = 0;

                        if (hasEducationMax ||
                            !ParseUnsigned32(
                                value,
                                &education))
                        {
                            return false;
                        }

                        hasEducationMax = true;
                        query->filter.educationMax = education;
                    }
                    else if (key == "job_nature")
                    {
                        std::uint32_t nature = 0;

                        if (hasJobNature ||
                            !ParseUnsigned32(
                                value,
                                &nature))
                        {
                            return false;
                        }

                        hasJobNature = true;
                        query->filter.jobNature = nature;
                    }
                    else if (key == "deadline_before")
                    {
                        if (hasDeadlineBefore)
                        {
                            return false;
                        }

                        hasDeadlineBefore = true;
                        query->filter.deadlineBefore = value;
                    }
                    else if (key == "deadline_after")
                    {
                        if (hasDeadlineAfter)
                        {
                            return false;
                        }

                        hasDeadlineAfter = true;
                        query->filter.deadlineAfter = value;
                    }
                    else if (key == "open_only")
                    {
                        if (hasOpenOnly ||
                            (value != "true" &&
                             value != "false"))
                        {
                            return false;
                        }

                        hasOpenOnly = true;
                        query->filter.openOnly =
                            (value == "true");
                    }
                    else if (key == "sort")
                    {
                        if (hasSort)
                        {
                            return false;
                        }

                        hasSort = true;

                        if (value == "created_desc")
                        {
                            query->filter.sort =
                                JobSort::kCreatedDesc;
                        }
                        else if (value == "deadline_asc")
                        {
                            query->filter.sort =
                                JobSort::kDeadlineAsc;
                        }
                        else if (value == "education_asc")
                        {
                            query->filter.sort =
                                JobSort::kEducationAsc;
                        }
                        else
                        {
                            return false;
                        }
                    }
                    else if (key == "include_stale")
                    {
                        if (hasIncludeStale ||
                            (value != "true" &&
                             value != "false"))
                        {
                            return false;
                        }

                        hasIncludeStale = true;
                        query->filter.includeStale =
                            (value == "true");
                    }
                    else if (key == "posted_within_days")
                    {
                        std::uint32_t days = 0;

                        if (hasPostedWithinDays ||
                            !ParseUnsigned32(
                                value,
                                &days))
                        {
                            return false;
                        }

                        hasPostedWithinDays = true;
                        query->filter.postedWithinDays = days;
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

        std::optional<std::uint64_t> ParseJobId(
            const std::string &path)
        {
            constexpr const char *kPrefix =
                "/api/v1/jobs/";

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

            if (text.empty())
            {
                return std::nullopt;
            }

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

        Json NullableUnsigned(
            const std::optional<std::uint32_t> &value)
        {
            return value.has_value()
                       ? Json(value.value())
                       : Json(nullptr);
        }

        Json NullableDouble(
            const std::optional<double> &value)
        {
            return value.has_value()
                       ? Json(value.value())
                       : Json(nullptr);
        }

        // docs/03 section 9.1: the response publishes Beijing-local values.
        // A date-only deadline renders as a bare calendar date so the noon
        // storage convention is never rendered as an expiry moment; an unknown
        // kind keeps the real instant and reports the kind as null. Both keys
        // are null together when no conversion is representable.
        void WriteDeadlineFields(
            const std::optional<std::string> &deadline,
            const std::optional<bool> &dateOnly,
            Json *deadlineJson,
            Json *dateOnlyJson)
        {
            *deadlineJson = Json(nullptr);
            *dateOnlyJson = Json(nullptr);

            if (!deadline.has_value())
            {
                return;
            }

            if (dateOnly.value_or(false))
            {
                const std::optional<std::string> beijingDate =
                    DeadlineToBeijingDate(deadline.value());

                if (!beijingDate.has_value())
                {
                    return;
                }

                *deadlineJson = Json(beijingDate.value());
            }
            else
            {
                const std::optional<std::string> beijingLocal =
                    DeadlineToBeijingLocal(deadline.value());

                if (!beijingLocal.has_value())
                {
                    return;
                }

                *deadlineJson = Json(beijingLocal.value());
            }

            if (dateOnly.has_value())
            {
                *dateOnlyJson = Json(dateOnly.value());
            }
        }

        Json SummaryToJson(
            const JobSummary &summary)
        {
            Json deadlineJson;
            Json deadlineDateOnlyJson;

            WriteDeadlineFields(
                summary.deadline,
                summary.deadlineDateOnly,
                &deadlineJson,
                &deadlineDateOnlyJson);

            return Json{
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
                {"source", summary.source},
                {"created_at", summary.createTime},
                {"updated_at", summary.updateTime},
                {"posted_at",
                 NullableString(summary.publishedAt)},
                {"first_seen_at", summary.createTime},
                {"last_seen_at",
                 NullableString(summary.lastSeenAt)},
                {"last_verified_at",
                 NullableString(summary.lastVerifiedAt)},
                {"freshness",
                 summary.freshness.has_value()
                     ? Json(ToString(
                           summary.freshness.value()))
                     : Json(nullptr)},
                {"source_status",
                 NullableString(summary.sourceStatus)},
                {"source_url",
                 NullableString(summary.sourceUrl)},
                {"education_required",
                 NullableUnsigned(summary.educationRequired)},
                {"experience_years_min",
                 NullableUnsigned(summary.experienceYearsMin)},
                {"experience_years_max",
                 NullableUnsigned(summary.experienceYearsMax)},
                {"job_nature",
                 NullableUnsigned(summary.jobNature)},
                {"deadline", deadlineJson},
                {"deadline_date_only", deadlineDateOnlyJson}};
        }

        std::optional<std::uint64_t> AuthenticateRequest(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse *response,
            JobRankingService *service)
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
    } // namespace

    JobController::JobController(
        JobService &service,
        JobRankingService *rankingService)
        : service_(service),
          rankingService_(rankingService)
    {
        // 0015A compatibility rollback switch. The predicate is on by default;
        // the environment variable restores the pre-0015A visibility without
        // touching stored data (ExecPlan 0015A section 10).
        const char *value =
            std::getenv("JOB_PUBLIC_INCLUDE_NEEDS_REVIEW");

        if (value != nullptr)
        {
            const std::string text = value;

            includeNeedsReview_ =
                text == "1" ||
                text == "true" ||
                text == "TRUE" ||
                text == "yes";
        }
    }

    void JobController::Search(
        const jobpilot::core::HttpRequest &request,
        jobpilot::core::HttpResponse &response)
    {
        JobSearchQuery query;
        query.filter.includeNeedsReview = includeNeedsReview_;

        if (!ParseSearchQuery(
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

        const ServiceResult<JobPage> result =
            service_.SearchJobs(
                query.filter,
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

        for (const JobSummary &job :
             result.value.list)
        {
            list.push_back(
                SummaryToJson(job));
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

    void JobController::Detail(
        const jobpilot::core::HttpRequest &request,
        jobpilot::core::HttpResponse &response)
    {
        const std::optional<std::uint64_t> jobId =
            ParseJobId(request.Path());

        if (!jobId.has_value())
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                ErrorCode::kInvalidParam,
                nullptr);
            return;
        }

        const ServiceResult<JobDetail> result =
            service_.GetJob(jobId.value());

        if (result.code != ErrorCode::kSuccess)
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                result.code,
                nullptr);
            return;
        }

        Json data =
            SummaryToJson(result.value.summary);
        data["description"] =
            NullableString(
                result.value.description);
        data["source_url"] =
            NullableString(
                result.value.sourceUrl);
        data["status"] =
            result.value.status;

        Json requirements = Json::array();

        for (const JobRequirement &requirement :
             result.value.requirements)
        {
            requirements.push_back(
                Json{
                    {"skill", requirement.skill},
                    {"level", requirement.level}});
        }

        data["requirements"] =
            std::move(requirements);

        WriteResponse(
            &response,
            request.KeepAlive(),
            ErrorCode::kSuccess,
            data);
    }

    void JobController::Recommended(
        const jobpilot::core::HttpRequest &request,
        jobpilot::core::HttpResponse &response)
    {
        if (rankingService_ == nullptr)
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                ErrorCode::kInternalError,
                nullptr);
            return;
        }

        const std::optional<std::uint64_t> userId =
            AuthenticateRequest(
                request,
                &response,
                rankingService_);

        if (!userId.has_value())
        {
            return;
        }

        JobSearchQuery query;
        query.filter.includeNeedsReview = includeNeedsReview_;

        if (!ParseSearchQuery(
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

        const ServiceResult<RankedJobPage> result =
            rankingService_->RankJobs(
                userId.value(),
                query.filter,
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

        for (const RankedJob &job :
             result.value.list)
        {
            Json item =
                SummaryToJson(job.job);
            item["score"] = job.score;
            item["score_version"] =
                job.scoreVersion;
            item["score_breakdown"] = Json{
                {"skill",
                 NullableDouble(
                     job.breakdown.skill)},
                {"title",
                 NullableDouble(
                     job.breakdown.title)},
                {"salary",
                 NullableDouble(
                     job.breakdown.salary)},
                {"location",
                 NullableDouble(
                     job.breakdown.location)},
                {"first_seen",
                 NullableDouble(
                     job.breakdown.firstSeen)}};
            item["matched_skills"] =
                job.matchedSkills;
            item["missing_skills"] =
                job.missingSkills;

            // 0015 B1/B3: deterministic explanation keys (additive; the
            // `rule_v1` score itself is unchanged).
            Json reasons = Json::array();

            for (const MatchReason &reason : job.reasons)
            {
                reasons.push_back(
                    Json{
                        {"code", reason.code},
                        {"text", reason.text},
                        {"evidence", reason.evidence}});
            }

            item["reasons"] = std::move(reasons);

            Json dealBreakers = Json::array();

            for (const MatchDealBreaker &breaker :
                 job.dealBreakers)
            {
                dealBreakers.push_back(
                    Json{
                        {"code", breaker.code},
                        {"text", breaker.text}});
            }

            item["deal_breakers"] = std::move(dealBreakers);
            item["deadline_urgency"] =
                ToString(job.deadlineUrgency);
            list.push_back(
                std::move(item));
        }

        WriteResponse(
            &response,
            request.KeepAlive(),
            ErrorCode::kSuccess,
            Json{
                {"list", std::move(list)},
                {"total", result.value.total},
                {"ranked_total",
                 result.value.rankedTotal},
                {"truncated",
                 result.value.truncated},
                {"page", result.value.page},
                {"page_size", result.value.pageSize}});
    }

} // namespace jobpilot::service
