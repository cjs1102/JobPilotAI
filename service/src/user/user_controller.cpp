#include "jobpilot/service/user/user_controller.h"

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>

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
            case ErrorCode::kUserNotFound:
            case ErrorCode::kInvalidPassword:
                return 401;
            case ErrorCode::kForbidden:
                return 403;
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
            case 409:
                return "Conflict";
            default:
                return "Internal Server Error";
            }
        }

        std::string Trim(
            const std::string &value)
        {
            std::size_t begin = 0;
            std::size_t end = value.size();

            while (begin < end &&
                   std::isspace(
                       static_cast<unsigned char>(
                           value[begin])))
            {
                ++begin;
            }

            while (end > begin &&
                   std::isspace(
                       static_cast<unsigned char>(
                           value[end - 1])))
            {
                --end;
            }

            return value.substr(
                begin,
                end - begin);
        }

        std::string ToLower(
            std::string value)
        {
            for (char &character : value)
            {
                character = static_cast<char>(
                    std::tolower(
                        static_cast<unsigned char>(
                            character)));
            }

            return value;
        }

        std::optional<std::string> ParseBearerToken(
            const std::string &authorization)
        {
            constexpr const char *kPrefix =
                "Bearer ";

            if (authorization.size() <= 7 ||
                ToLower(authorization.substr(0, 7)) !=
                    ToLower(kPrefix))
            {
                return std::nullopt;
            }

            const std::string token =
                Trim(authorization.substr(7));

            if (token.empty())
            {
                return std::nullopt;
            }

            return token;
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

} // namespace jobpilot::service
