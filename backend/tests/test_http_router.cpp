#include <cassert>
#include <iostream>

#include "jobpilot/http/http_request.h"
#include "jobpilot/http/http_response.h"
#include "jobpilot/http/http_router.h"

namespace jobpilot::core
{

HttpRequest MakeRequest(
    HttpMethod method,
    const std::string &path)
{
    HttpRequest request;
    request.SetMethod(method);
    request.SetPath(path);
    request.SetVersion(HttpVersion::kHttp11);
    return request;
}

void TestHealthRoute()
{
    HttpRouter router;

    router.RegisterRoute(
        HttpMethod::kGet,
        "/health",
        [](const HttpRequest &, HttpResponse &response)
        {
            response.SetStatusCode(200);
            response.SetStatusMessage("OK");
            response.SetBody("health");
        });

    HttpRequest request =
        MakeRequest(HttpMethod::kGet, "/health");

    HttpResponse response;

    const bool matched =
        router.Route(request, &response);

    assert(matched);
    assert(response.StatusCode() == 200);
    assert(response.Body() == "health");
}

void TestHelloRoute()
{
    HttpRouter router;

    router.RegisterRoute(
        HttpMethod::kGet,
        "/hello",
        [](const HttpRequest &, HttpResponse &response)
        {
            response.SetStatusCode(200);
            response.SetStatusMessage("OK");
            response.SetBody("hello");
        });

    HttpRequest request =
        MakeRequest(HttpMethod::kGet, "/hello");

    HttpResponse response;

    assert(router.Route(request, &response));
    assert(response.StatusCode() == 200);
    assert(response.Body() == "hello");
}

void TestParameterRoute()
{
    HttpRouter router;

    router.RegisterRoute(
        HttpMethod::kGet,
        "/api/v1/jobs/{id}",
        [](const HttpRequest &, HttpResponse &response)
        {
            response.SetStatusCode(200);
            response.SetStatusMessage("OK");
            response.SetBody("job");
        });

    HttpRequest request =
        MakeRequest(
            HttpMethod::kGet,
            "/api/v1/jobs/123");

    HttpResponse response;

    assert(router.Route(request, &response));
    assert(response.StatusCode() == 200);
    assert(response.Body() == "job");
}

void TestStaticRouteHasPriority()
{
    HttpRouter router;

    router.RegisterRoute(
        HttpMethod::kGet,
        "/api/v1/jobs/{id}",
        [](const HttpRequest &, HttpResponse &response)
        {
            response.SetStatusCode(200);
            response.SetStatusMessage("OK");
            response.SetBody("parameter");
        });

    router.RegisterRoute(
        HttpMethod::kGet,
        "/api/v1/jobs/list",
        [](const HttpRequest &, HttpResponse &response)
        {
            response.SetStatusCode(200);
            response.SetStatusMessage("OK");
            response.SetBody("static");
        });

    HttpRequest request =
        MakeRequest(
            HttpMethod::kGet,
            "/api/v1/jobs/list");

    HttpResponse response;

    assert(router.Route(request, &response));
    assert(response.Body() == "static");
}

void TestMethodMismatch()
{
    HttpRouter router;

    router.RegisterRoute(
        HttpMethod::kGet,
        "/hello",
        [](const HttpRequest &, HttpResponse &response)
        {
            response.SetStatusCode(200);
            response.SetStatusMessage("OK");
        });

    HttpRequest request =
        MakeRequest(HttpMethod::kPost, "/hello");

    HttpResponse response;

    assert(!router.Route(request, &response));
}

void TestNotFound()
{
    HttpRouter router;

    router.RegisterRoute(
        HttpMethod::kGet,
        "/hello",
        [](const HttpRequest &, HttpResponse &response)
        {
            response.SetStatusCode(200);
            response.SetStatusMessage("OK");
        });

    HttpRequest request =
        MakeRequest(HttpMethod::kGet, "/not-found");

    HttpResponse response;

    assert(!router.Route(request, &response));
}

void TestPutMethodDispatch()
{
    HttpRouter router;

    router.RegisterRoute(
        HttpMethod::kPut,
        "/profile",
        [](const HttpRequest &, HttpResponse &response)
        {
            response.SetBody("updated");
        });

    HttpRequest request =
        MakeRequest(HttpMethod::kPut, "/profile");

    HttpResponse response;

    assert(router.Route(request, &response));
    assert(response.Body() == "updated");
}

void TestDeleteMethodDispatch()
{
    HttpRouter router;

    router.RegisterRoute(
        HttpMethod::kDelete,
        "/api/v1/users/skills/{id}",
        [](const HttpRequest &, HttpResponse &response)
        {
            response.SetBody("deleted");
        });

    HttpRequest request =
        MakeRequest(
            HttpMethod::kDelete,
            "/api/v1/users/skills/42");

    HttpResponse response;

    assert(router.Route(request, &response));
    assert(response.Body() == "deleted");
}

void TestQueryStringDoesNotBreakRouteMatching()
{
    HttpRouter router;

    router.RegisterRoute(
        HttpMethod::kGet,
        "/api/v1/resumes",
        [](const HttpRequest &, HttpResponse &response)
        {
            response.SetBody("resumes");
        });

    HttpRequest request =
        MakeRequest(
            HttpMethod::kGet,
            "/api/v1/resumes?page=1&page_size=20");

    HttpResponse response;

    assert(router.Route(request, &response));
    assert(response.Body() == "resumes");
}

}  // namespace jobpilot::core

int main()
{
    using namespace jobpilot::core;

    TestHealthRoute();
    TestHelloRoute();
    TestParameterRoute();
    TestStaticRouteHasPriority();
    TestMethodMismatch();
    TestNotFound();
    TestPutMethodDispatch();
    TestDeleteMethodDispatch();
    TestQueryStringDoesNotBreakRouteMatching();

    std::cout << "http router tests passed\n";

    return 0;
}
