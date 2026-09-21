#include "jobpilot/http/http_server.h"
#include "jobpilot/network/event_loop.h"
#include "jobpilot/service/router.h"

int main()
{
    jobpilot::core::EventLoop loop;

    jobpilot::core::HttpServer server(
        &loop,
        "0.0.0.0",
        8080);

    jobpilot::service::RegisterRoutes(&server);

    server.Start();
    loop.Loop();

    return 0;
}