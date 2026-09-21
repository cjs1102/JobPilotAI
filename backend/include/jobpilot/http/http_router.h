#pragma once

#include <functional>
#include <string>
#include <vector>

#include "jobpilot/http/http_request.h"
#include "jobpilot/http/http_response.h"

namespace jobpilot::core
{

    class HttpRouter
    {
    public:
        using Handler =
            std::function<void(
                const HttpRequest &,
                HttpResponse &)>;

        HttpRouter() = default;

        void RegisterRoute(
            HttpMethod method,
            const std::string &path,
            const Handler &handler);

        bool Route(
            const HttpRequest &request,
            HttpResponse *response) const;

    private:
        struct RouteEntry
        {
            HttpMethod method;
            std::string path;
            Handler handler;
            bool parameterRoute;
        };

        std::vector<RouteEntry> routes_;
    };

} // namespace jobpilot::core