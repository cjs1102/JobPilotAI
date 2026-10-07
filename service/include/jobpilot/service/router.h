#pragma once

#include "jobpilot/http/http_server.h"

namespace jobpilot::service
{
    class ResumeController;
    class UserController;

    void RegisterRoutes(
        jobpilot::core::HttpServer *server,
        UserController *userController,
        ResumeController *resumeController = nullptr);

} // namespace jobpilot::service
