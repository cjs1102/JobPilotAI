#include "jobpilot/service/router.h"

#include <stdexcept>

#include "jobpilot/http/http_request.h"
#include "jobpilot/http/http_response.h"
#include "jobpilot/service/user/user_controller.h"

namespace jobpilot::service
{

    void RegisterRoutes(
        jobpilot::core::HttpServer *server,
        UserController *userController)
    {
        if (server == nullptr ||
            userController == nullptr)
        {
            throw std::invalid_argument(
                "RegisterRoutes requires server and user controller");
        }

        auto &router = server->Router();

        router.RegisterRoute(
            jobpilot::core::HttpMethod::kPost,
            "/api/v1/users/register",
            [userController](
                const jobpilot::core::HttpRequest &request,
                jobpilot::core::HttpResponse &response)
            {
                userController->Register(
                    request,
                    response);
            });

        router.RegisterRoute(
            jobpilot::core::HttpMethod::kPost,
            "/api/v1/users/login",
            [userController](
                const jobpilot::core::HttpRequest &request,
                jobpilot::core::HttpResponse &response)
            {
                userController->Login(
                    request,
                    response);
            });

        router.RegisterRoute(
            jobpilot::core::HttpMethod::kGet,
            "/api/v1/users/me",
            [userController](
                const jobpilot::core::HttpRequest &request,
                jobpilot::core::HttpResponse &response)
            {
                userController->Me(
                    request,
                    response);
            });

        router.RegisterRoute(
            jobpilot::core::HttpMethod::kGet,
            "/api/v1/users/profile",
            [userController](
                const jobpilot::core::HttpRequest &request,
                jobpilot::core::HttpResponse &response)
            {
                userController->GetProfile(
                    request,
                    response);
            });

        router.RegisterRoute(
            jobpilot::core::HttpMethod::kPut,
            "/api/v1/users/profile",
            [userController](
                const jobpilot::core::HttpRequest &request,
                jobpilot::core::HttpResponse &response)
            {
                userController->UpdateProfile(
                    request,
                    response);
            });

        router.RegisterRoute(
            jobpilot::core::HttpMethod::kGet,
            "/api/v1/users/skills",
            [userController](
                const jobpilot::core::HttpRequest &request,
                jobpilot::core::HttpResponse &response)
            {
                userController->ListSkills(
                    request,
                    response);
            });

        router.RegisterRoute(
            jobpilot::core::HttpMethod::kPost,
            "/api/v1/users/skills",
            [userController](
                const jobpilot::core::HttpRequest &request,
                jobpilot::core::HttpResponse &response)
            {
                userController->CreateSkill(
                    request,
                    response);
            });

        router.RegisterRoute(
            jobpilot::core::HttpMethod::kDelete,
            "/api/v1/users/skills/{id}",
            [userController](
                const jobpilot::core::HttpRequest &request,
                jobpilot::core::HttpResponse &response)
            {
                userController->DeleteSkill(
                    request,
                    response);
            });

        router.RegisterRoute(
            jobpilot::core::HttpMethod::kGet,
            "/health",
            [](const jobpilot::core::HttpRequest &request,
               jobpilot::core::HttpResponse &response)
            {
                (void)request;

                response.SetStatusCode(200);
                response.SetStatusMessage("OK");
                response.SetHeader(
                    "Content-Type",
                    "application/json");
                response.SetBody(
                    "{\"code\":0,"
                    "\"message\":\"success\","
                    "\"data\":{\"status\":\"ok\"}}");
                response.SetKeepAlive(true);
            });

        router.RegisterRoute(
            jobpilot::core::HttpMethod::kPost,
            "/hello",
            [](const jobpilot::core::HttpRequest &request,
               jobpilot::core::HttpResponse &response)
            {
                response.SetStatusCode(200);
                response.SetStatusMessage("OK");
                response.SetHeader(
                    "Content-Type",
                    "application/json");

                response.SetBody(
                    request.Body());

                response.SetKeepAlive(
                    request.KeepAlive());
            });

        router.RegisterRoute(
            jobpilot::core::HttpMethod::kGet,
            "/api/v1/jobs/{id}",
            [](const jobpilot::core::HttpRequest &request,
               jobpilot::core::HttpResponse &response)
            {
                (void)request;

                response.SetStatusCode(200);
                response.SetStatusMessage("OK");
                response.SetHeader(
                    "Content-Type",
                    "application/json");
                response.SetBody(
                    "{\"code\":0,\"message\":\"job found\"}");
                response.SetKeepAlive(true);
            });
    }

} // namespace jobpilot::service
