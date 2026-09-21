#pragma once

#include "jobpilot/http/http_server.h"

namespace jobpilot::service
{

    void RegisterRoutes(jobpilot::core::HttpServer *server);

} // namespace jobpilot::service