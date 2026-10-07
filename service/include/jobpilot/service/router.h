#pragma once

#include "jobpilot/http/http_server.h"

namespace jobpilot::service
{
    class ApplicationController;
    class JobController;
    class ResumeController;
    class UserController;

    void RegisterRoutes(
        jobpilot::core::HttpServer *server,
        UserController *userController,
        ResumeController *resumeController = nullptr,
        JobController *jobController = nullptr,
        ApplicationController *applicationController = nullptr);

} // namespace jobpilot::service
