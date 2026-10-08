#pragma once

#include "jobpilot/http/http_request.h"
#include "jobpilot/http/http_response.h"
#include "jobpilot/service/ai/ai_task_service.h"

namespace jobpilot::service
{
    class ISessionAuthenticator;

    // Public, bearer-authenticated AI endpoints (ExecPlan 0013 section 5.2;
    // docs/04 section 7).
    class AiController
    {
    public:
        AiController(
            AiTaskService &service,
            ISessionAuthenticator &authenticator);

        // POST /api/v1/ai/match
        void CreateMatch(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

        // GET /api/v1/ai/tasks/{id}
        void GetTask(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

    private:
        AiTaskService &service_;
        ISessionAuthenticator &authenticator_;
    };

} // namespace jobpilot::service
