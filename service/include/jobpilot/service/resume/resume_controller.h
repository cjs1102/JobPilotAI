#pragma once

#include "jobpilot/http/http_request.h"
#include "jobpilot/http/http_response.h"
#include "jobpilot/service/resume/resume_artifact_service.h"
#include "jobpilot/service/resume/resume_service.h"

namespace jobpilot::service
{
    class ResumeController
    {
    public:
        // `artifactService` is optional so existing constructions stay valid;
        // 0015 D5 wires it and the artifact routes need it.
        explicit ResumeController(
            ResumeService &service,
            ResumeArtifactService *artifactService = nullptr);

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

        // 0015 D5 tailored-artifact surface (authenticated user routes).
        void CreateArtifact(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

        void ListArtifacts(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

        void GetArtifact(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

        void ConfirmArtifact(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

    private:
        ResumeService &service_;
        ResumeArtifactService *artifactService_;
    };

} // namespace jobpilot::service
