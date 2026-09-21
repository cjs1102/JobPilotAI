#include "jobpilot/service/router.h"

#include "jobpilot/http/http_request.h"
#include "jobpilot/http/http_response.h"

namespace jobpilot::service
{

    void RegisterRoutes(jobpilot::core::HttpServer *server)
    {
        auto &router = server->Router();

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