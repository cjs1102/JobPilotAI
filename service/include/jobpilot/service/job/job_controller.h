#pragma once

#include "jobpilot/http/http_request.h"
#include "jobpilot/http/http_response.h"
#include "jobpilot/service/job/job_service.h"

namespace jobpilot::service
{
    class JobController
    {
    public:
        explicit JobController(JobService &service);

        void Search(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

        void Detail(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

    private:
        JobService &service_;
    };

} // namespace jobpilot::service
