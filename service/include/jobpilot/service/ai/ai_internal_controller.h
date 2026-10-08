#pragma once

#include <string>

#include "jobpilot/http/http_request.h"
#include "jobpilot/http/http_response.h"
#include "jobpilot/service/ai/ai_task_service.h"

namespace jobpilot::service
{
    // Internal worker API, X-Internal-Token protected with the same trust
    // boundary as /internal/jobs/source-sync (ExecPlan 0013 section 5.3;
    // docs/04 section 10.3).
    class AiInternalController
    {
    public:
        AiInternalController(
            AiTaskService &service,
            std::string internalToken);

        // POST /internal/ai/tasks/claim
        void Claim(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

        // POST /internal/ai/tasks/{id}/complete
        void Complete(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

        // POST /internal/ai/tasks/{id}/fail
        void Fail(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

        // GET /internal/ai/tasks/{id}/input?worker_id=...
        void Input(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

    private:
        AiTaskService &service_;
        std::string internalToken_;
    };

} // namespace jobpilot::service
