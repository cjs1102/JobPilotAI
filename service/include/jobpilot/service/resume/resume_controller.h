#pragma once

#include "jobpilot/http/http_request.h"
#include "jobpilot/http/http_response.h"
#include "jobpilot/service/resume/resume_service.h"

namespace jobpilot::service
{
    class ResumeController
    {
    public:
        explicit ResumeController(
            ResumeService &service);

        void Upload(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

        void List(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

        void CreateVersion(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

        void ListVersions(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

    private:
        ResumeService &service_;
    };

} // namespace jobpilot::service
