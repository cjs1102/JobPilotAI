#pragma once

#include "jobpilot/http/http_request.h"
#include "jobpilot/http/http_response.h"
#include "jobpilot/service/application/application_service.h"

namespace jobpilot::service
{
    class ApplicationController
    {
    public:
        explicit ApplicationController(
            ApplicationService &service);

        void Create(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

        void List(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

        void UpdateStatus(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

        void Statistics(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

    private:
        ApplicationService &service_;
    };

} // namespace jobpilot::service
