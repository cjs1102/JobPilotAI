#include "jobpilot/service/user/user_controller.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
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
            // Same mapping as the AI and internal controllers (a user endpoint
            // cannot currently produce an AI code; kept exhaustive so future
            // enumerators cannot reintroduce the -Wswitch warning).
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

        bool TryGetString(
            const Json &body,
            const char *key,
            std::string *value)
        {
            const auto iterator = body.find(key);

            if (iterator == body.end() ||
                !iterator->is_string())
            {
                return false;
            }

            *value = iterator->get<std::string>();
            return true;
        }

        bool TryGetOptionalString(
            const Json &body,
            const char *key,
            std::optional<std::string> *value)
        {
            const auto iterator = body.find(key);

            if (iterator == body.end() ||
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

        bool TryGetOptionalUInt32(
            const Json &body,
            const char *key,
            std::optional<std::uint32_t> *value)
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

            const std::uint64_t parsed =
                iterator->get<std::uint64_t>();

            if (parsed >
                std::numeric_limits<
                    std::uint32_t>::max())
            {
                return false;
            }

            *value =
                static_cast<std::uint32_t>(parsed);
            return true;
        }

        bool TryGetInt32(
            const Json &body,
            const char *key,
            std::int32_t *value)
        {
            const auto iterator = body.find(key);

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

        void WriteResponse(
            jobpilot::core::HttpResponse *response,
            bool keepAlive,
            ErrorCode code,
            const Json &data)
        {
            const int statusCode =
                HttpStatusFor(code);

            Json body{
                {"code",
                 static_cast<int>(code)},
                {"message",
                 ErrorMessage(code)},
                {"data", data}};

            response->SetStatusCode(statusCode);
            response->SetStatusMessage(
                HttpReason(statusCode));
            response->SetHeader(
                "Content-Type",
                "application/json");
            response->SetBody(body.dump());
            response->SetKeepAlive(keepAlive);
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

        std::optional<std::uint64_t> AuthenticateRequest(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse *response,
            UserService *service)
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

            const ServiceResult<std::uint64_t> authenticated =
                service->Authenticate(token.value());

            if (authenticated.code != ErrorCode::kSuccess)
            {
                WriteResponse(
                    response,
                    request.KeepAlive(),
                    authenticated.code,
                    nullptr);

                return std::nullopt;
            }

            return authenticated.value;
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

        Json ProfileToJson(
            const UserProfile &profile)
        {
            return Json{
                {"target_position",
                 NullableString(
                     profile.targetPosition)},
                {"experience",
                 NullableString(profile.experience)},
                {"location",
                 NullableString(profile.location)},
                {"salary_min",
                 NullableUnsigned(profile.salaryMin)},
                {"salary_max",
                 NullableUnsigned(profile.salaryMax)},
                {"summary",
                 NullableString(profile.summary)},
                {"education_level",
                 NullableUnsigned(profile.educationLevel)},
                {"graduation_year",
                 NullableUnsigned(profile.graduationYear)},
                {"campus_status",
                 NullableUnsigned(profile.campusStatus)}};
        }

        Json SkillToJson(
            const UserSkill &skill)
        {
            return Json{
                {"id", skill.id},
                {"name", skill.name},
                {"level", skill.level}};
        }

        std::optional<std::uint64_t> ParseSkillId(
            const std::string &path)
        {
            constexpr const char *kPrefix =
                "/api/v1/users/skills/";

            if (path.rfind(kPrefix, 0) != 0)
            {
                return std::nullopt;
            }

            const std::string text =
                path.substr(
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
    } // namespace

    UserController::UserController(
        UserService &service)
        : service_(service)
    {
    }

    void UserController::Register(
        const jobpilot::core::HttpRequest &request,
        jobpilot::core::HttpResponse &response)
    {
        Json body;

        if (!ParseJsonBody(
                request,
                &response,
                &body))
        {
            return;
        }

        std::string username;
        std::string email;
        std::string password;

        if (!TryGetString(
                body,
                "username",
                &username) ||
            !TryGetString(
                body,
                "email",
                &email) ||
            !TryGetString(
                body,
                "password",
                &password))
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                ErrorCode::kInvalidParam,
                nullptr);

            return;
        }

        const ServiceResult<std::uint64_t> result =
            service_.Register(
                username,
                email,
                password);

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
            Json{{"user_id", result.value}});
    }

    void UserController::Login(
        const jobpilot::core::HttpRequest &request,
        jobpilot::core::HttpResponse &response)
    {
        Json body;

        if (!ParseJsonBody(
                request,
                &response,
                &body))
        {
            return;
        }

        std::string email;
        std::string password;

        if (!TryGetString(
                body,
                "email",
                &email) ||
            !TryGetString(
                body,
                "password",
                &password))
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                ErrorCode::kInvalidParam,
                nullptr);

            return;
        }

        const ServiceResult<LoginData> result =
            service_.Login(
                email,
                password);

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
                {"token", result.value.token},
                {"user_id", result.value.userId}});
    }

    void UserController::Me(
        const jobpilot::core::HttpRequest &request,
        jobpilot::core::HttpResponse &response)
    {
        const std::optional<std::string> token =
            ParseBearerToken(
                request.GetHeader("Authorization"));

        if (!token.has_value())
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                ErrorCode::kUnauthorized,
                nullptr);

            return;
        }

        const ServiceResult<UserRecord> result =
            service_.GetCurrentUser(
                token.value());

        if (result.code != ErrorCode::kSuccess)
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                result.code,
                nullptr);

            return;
        }

        const UserRecord &user = result.value;

        WriteResponse(
            &response,
            request.KeepAlive(),
            ErrorCode::kSuccess,
            Json{
                {"id", user.id},
                {"username", user.username},
                {"email", user.email},
                {"avatar",
                 user.avatar.empty()
                     ? Json(nullptr)
                     : Json(user.avatar)},
                {"created_at", user.createTime}});
    }

    void UserController::GetProfile(
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

        const ServiceResult<UserProfile> result =
            service_.GetProfile(userId.value());

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
            ProfileToJson(result.value));
    }

    void UserController::UpdateProfile(
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

        UserProfile profile;

        if (!TryGetOptionalString(
                body,
                "target_position",
                &profile.targetPosition) ||
            !TryGetOptionalString(
                body,
                "experience",
                &profile.experience) ||
            !TryGetOptionalString(
                body,
                "location",
                &profile.location) ||
            !TryGetOptionalUInt32(
                body,
                "salary_min",
                &profile.salaryMin) ||
            !TryGetOptionalUInt32(
                body,
                "salary_max",
                &profile.salaryMax) ||
            !TryGetOptionalString(
                body,
                "summary",
                &profile.summary) ||
            !TryGetOptionalUInt32(
                body,
                "education_level",
                &profile.educationLevel) ||
            !TryGetOptionalUInt32(
                body,
                "graduation_year",
                &profile.graduationYear) ||
            !TryGetOptionalUInt32(
                body,
                "campus_status",
                &profile.campusStatus))
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                ErrorCode::kInvalidParam,
                nullptr);

            return;
        }

        const ServiceResult<UserProfile> result =
            service_.UpdateProfile(
                userId.value(),
                profile);

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
            ProfileToJson(result.value));
    }

    void UserController::ListSkills(
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

        const ServiceResult<std::vector<UserSkill>> result =
            service_.ListSkills(userId.value());

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

        for (const UserSkill &skill : result.value)
        {
            list.push_back(SkillToJson(skill));
        }

        WriteResponse(
            &response,
            request.KeepAlive(),
            ErrorCode::kSuccess,
            Json{{"list", std::move(list)}});
    }

    void UserController::CreateSkill(
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

        std::string name;
        std::int32_t level = 0;

        if (!TryGetString(
                body,
                "name",
                &name) ||
            !TryGetInt32(
                body,
                "level",
                &level))
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                ErrorCode::kInvalidParam,
                nullptr);

            return;
        }

        const ServiceResult<UserSkill> result =
            service_.UpsertSkill(
                userId.value(),
                name,
                level);

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
            SkillToJson(result.value));
    }

    void UserController::DeleteSkill(
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

        const std::optional<std::uint64_t> skillId =
            ParseSkillId(request.Path());

        if (!skillId.has_value())
        {
            WriteResponse(
                &response,
                request.KeepAlive(),
                ErrorCode::kInvalidParam,
                nullptr);

            return;
        }

        const ServiceResult<bool> result =
            service_.DeleteSkill(
                userId.value(),
                skillId.value());

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
            nullptr);
    }

} // namespace jobpilot::service
