#pragma once

#include "jobpilot/http/http_server.h"

namespace jobpilot::service
{
    class UserController;

    void RegisterRoutes(
        jobpilot::core::HttpServer *server,
        UserController *userController);

} // namespace jobpilot::service
