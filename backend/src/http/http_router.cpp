#include "jobpilot/http/http_router.h"

#include <string>

namespace jobpilot::core
{

namespace
{

bool IsParameterSegment(const std::string &segment)
{
    return segment.size() >= 2 &&
           segment.front() == '{' &&
           segment.back() == '}';
}

bool MatchPath(
    const std::string &routePath,
    const std::string &requestPath)
{
    if (routePath == requestPath)
    {
        return true;
    }

    std::size_t routeStart = 0;
    std::size_t requestStart = 0;

    while (true)
    {
        const std::size_t routeSlash =
            routePath.find('/', routeStart);
        const std::size_t requestSlash =
            requestPath.find('/', requestStart);

        const std::string routeSegment =
            routePath.substr(
                routeStart,
                routeSlash == std::string::npos
                    ? std::string::npos
                    : routeSlash - routeStart);

        const std::string requestSegment =
            requestPath.substr(
                requestStart,
                requestSlash == std::string::npos
                    ? std::string::npos
                    : requestSlash - requestStart);

        if (IsParameterSegment(routeSegment))
        {
            if (requestSegment.empty())
            {
                return false;
            }
        }
        else if (routeSegment != requestSegment)
        {
            return false;
        }

        const bool routeEnd =
            routeSlash == std::string::npos;
        const bool requestEnd =
            requestSlash == std::string::npos;

        if (routeEnd || requestEnd)
        {
            return routeEnd && requestEnd;
        }

        routeStart = routeSlash + 1;
        requestStart = requestSlash + 1;
    }
}

}  // namespace

void HttpRouter::RegisterRoute(
    HttpMethod method,
    const std::string &path,
    const Handler &handler)
{
    routes_.push_back(
        RouteEntry{
            method,
            path,
            handler,
            path.find('{') != std::string::npos});
}

bool HttpRouter::Route(
    const HttpRequest &request,
    HttpResponse *response) const
{
    if (response == nullptr)
    {
        return false;
    }

    // 1. 先进行静态路径匹配。
    //    这样可以保证静态路径优先于参数路径。
    for (const auto &route : routes_)
    {
        if (route.method != request.Method())
        {
            continue;
        }

        if (route.parameterRoute)
        {
            continue;
        }

        if (route.path == request.Path())
        {
            route.handler(request, *response);
            return true;
        }
    }

    // 2. 再进行参数路径匹配。
    for (const auto &route : routes_)
    {
        if (route.method != request.Method())
        {
            continue;
        }

        if (!route.parameterRoute)
        {
            continue;
        }

        if (MatchPath(route.path, request.Path()))
        {
            route.handler(request, *response);
            return true;
        }
    }

    return false;
}

}  // namespace jobpilot::core
