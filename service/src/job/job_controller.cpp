#include "jobpilot/service/job/job_controller.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "jobpilot/error.h"
#include "jobpilot/service/auth/session_authenticator.h"
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

        Json SummaryToJson(
            const JobSummary &summary)
        {
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
                 NullableString(summary.sourceStatus)}};
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
    }

    void JobController::Search(
        const jobpilot::core::HttpRequest &request,
        jobpilot::core::HttpResponse &response)
    {
        JobSearchQuery query;

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
