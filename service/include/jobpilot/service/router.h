#pragma once

#include "jobpilot/http/http_server.h"

namespace jobpilot::service
{
    class ApplicationController;
    class InternalJobImportController;
    class InternalSourceSyncController;
    class JobController;
    class ResumeController;
    class UserController;

    void RegisterRoutes(
        jobpilot::core::HttpServer *server,
        UserController *userController,
        ResumeController *resumeController = nullptr,
        JobController *jobController = nullptr,
        ApplicationController *applicationController = nullptr,
        InternalJobImportController *internalJobImportController = nullptr,
        InternalSourceSyncController *internalSourceSyncController = nullptr);

} // namespace jobpilot::service
