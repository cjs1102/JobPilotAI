#include "jobpilot/service/router.h"

#include <stdexcept>

#include "jobpilot/http/http_request.h"
#include "jobpilot/http/http_response.h"
#include "jobpilot/service/ai/ai_controller.h"
#include "jobpilot/service/ai/ai_internal_controller.h"
#include "jobpilot/service/application/application_controller.h"
#include "jobpilot/service/job/job_controller.h"
#include "jobpilot/service/job/internal_job_import_controller.h"
#include "jobpilot/service/job/internal_job_review_controller.h"
#include "jobpilot/service/job/internal_source_sync_controller.h"
#include "jobpilot/service/resume/resume_controller.h"
#include "jobpilot/service/user/user_controller.h"

namespace jobpilot::service
{

    void RegisterRoutes(
        jobpilot::core::HttpServer *server,
        UserController *userController,
        ResumeController *resumeController,
        JobController *jobController,
        ApplicationController *applicationController,
        InternalJobImportController *internalJobImportController,
        InternalSourceSyncController *internalSourceSyncController,
        AiController *aiController,
        AiInternalController *aiInternalController,
        InternalJobReviewController *internalJobReviewController)
    {
        if (server == nullptr ||
            userController == nullptr)
        {
            throw std::invalid_argument(
                "RegisterRoutes requires server and user controller");
        }

        auto &router = server->Router();

        router.RegisterRoute(
            jobpilot::core::HttpMethod::kPost,
            "/api/v1/users/register",
            [userController](
                const jobpilot::core::HttpRequest &request,
                jobpilot::core::HttpResponse &response)
            {
                userController->Register(
                    request,
                    response);
            });

        router.RegisterRoute(
            jobpilot::core::HttpMethod::kPost,
            "/api/v1/users/login",
            [userController](
                const jobpilot::core::HttpRequest &request,
                jobpilot::core::HttpResponse &response)
            {
                userController->Login(
                    request,
                    response);
            });

        router.RegisterRoute(
            jobpilot::core::HttpMethod::kGet,
            "/api/v1/users/me",
            [userController](
                const jobpilot::core::HttpRequest &request,
                jobpilot::core::HttpResponse &response)
            {
                userController->Me(
                    request,
                    response);
            });

        router.RegisterRoute(
            jobpilot::core::HttpMethod::kGet,
            "/api/v1/users/profile",
            [userController](
                const jobpilot::core::HttpRequest &request,
                jobpilot::core::HttpResponse &response)
            {
                userController->GetProfile(
                    request,
                    response);
            });

        router.RegisterRoute(
            jobpilot::core::HttpMethod::kPut,
            "/api/v1/users/profile",
            [userController](
                const jobpilot::core::HttpRequest &request,
                jobpilot::core::HttpResponse &response)
            {
                userController->UpdateProfile(
                    request,
                    response);
            });

        router.RegisterRoute(
            jobpilot::core::HttpMethod::kGet,
            "/api/v1/users/skills",
            [userController](
                const jobpilot::core::HttpRequest &request,
                jobpilot::core::HttpResponse &response)
            {
                userController->ListSkills(
                    request,
                    response);
            });

        router.RegisterRoute(
            jobpilot::core::HttpMethod::kPost,
            "/api/v1/users/skills",
            [userController](
                const jobpilot::core::HttpRequest &request,
                jobpilot::core::HttpResponse &response)
            {
                userController->CreateSkill(
                    request,
                    response);
            });

        router.RegisterRoute(
            jobpilot::core::HttpMethod::kDelete,
            "/api/v1/users/skills/{id}",
            [userController](
                const jobpilot::core::HttpRequest &request,
                jobpilot::core::HttpResponse &response)
            {
                userController->DeleteSkill(
                    request,
                    response);
            });

        router.RegisterRoute(
            jobpilot::core::HttpMethod::kGet,
            "/health",
            [](const jobpilot::core::HttpRequest &request,
               jobpilot::core::HttpResponse &response)
            {
                (void)request;

                response.SetStatusCode(200);
                response.SetStatusMessage("OK");
                response.SetHeader(
                    "Content-Type",
                    "application/json");
                response.SetBody(
                    "{\"code\":0,"
                    "\"message\":\"success\","
                    "\"data\":{\"status\":\"ok\"}}");
                response.SetKeepAlive(true);
            });

        if (resumeController != nullptr)
        {
            router.RegisterRoute(
                jobpilot::core::HttpMethod::kPost,
                "/api/v1/resumes/upload",
                [resumeController](
                    const jobpilot::core::HttpRequest &request,
                    jobpilot::core::HttpResponse &response)
                {
                    resumeController->Upload(
                        request,
                        response);
                });

            router.RegisterRoute(
                jobpilot::core::HttpMethod::kGet,
                "/api/v1/resumes",
                [resumeController](
                    const jobpilot::core::HttpRequest &request,
                    jobpilot::core::HttpResponse &response)
                {
                    resumeController->List(
                        request,
                        response);
                });

            router.RegisterRoute(
                jobpilot::core::HttpMethod::kPost,
                "/api/v1/resumes/{id}/versions",
                [resumeController](
                    const jobpilot::core::HttpRequest &request,
                    jobpilot::core::HttpResponse &response)
                {
                    resumeController->CreateVersion(
                        request,
                        response);
                });

            router.RegisterRoute(
                jobpilot::core::HttpMethod::kGet,
                "/api/v1/resumes/{id}/versions",
                [resumeController](
                    const jobpilot::core::HttpRequest &request,
                    jobpilot::core::HttpResponse &response)
                {
                    resumeController->ListVersions(
                        request,
                        response);
                });
        }

        if (jobController != nullptr)
        {
            router.RegisterRoute(
                jobpilot::core::HttpMethod::kGet,
                "/api/v1/jobs",
                [jobController](
                    const jobpilot::core::HttpRequest &request,
                    jobpilot::core::HttpResponse &response)
                {
                    jobController->Search(
                        request,
                        response);
                });

            router.RegisterRoute(
                jobpilot::core::HttpMethod::kGet,
                "/api/v1/jobs/recommended",
                [jobController](
                    const jobpilot::core::HttpRequest &request,
                    jobpilot::core::HttpResponse &response)
                {
                    jobController->Recommended(
                        request,
                        response);
                });

            router.RegisterRoute(
                jobpilot::core::HttpMethod::kGet,
                "/api/v1/jobs/{id}",
                [jobController](
                    const jobpilot::core::HttpRequest &request,
                    jobpilot::core::HttpResponse &response)
                {
                    jobController->Detail(
                        request,
                        response);
                });
        }

        if (applicationController != nullptr)
        {
            router.RegisterRoute(
                jobpilot::core::HttpMethod::kPost,
                "/api/v1/applications",
                [applicationController](
                    const jobpilot::core::HttpRequest &request,
                    jobpilot::core::HttpResponse &response)
                {
                    applicationController->Create(
                        request,
                        response);
                });

            router.RegisterRoute(
                jobpilot::core::HttpMethod::kGet,
                "/api/v1/applications",
                [applicationController](
                    const jobpilot::core::HttpRequest &request,
                    jobpilot::core::HttpResponse &response)
                {
                    applicationController->List(
                        request,
                        response);
                });

            router.RegisterRoute(
                jobpilot::core::HttpMethod::kGet,
                "/api/v1/applications/statistics",
                [applicationController](
                    const jobpilot::core::HttpRequest &request,
                    jobpilot::core::HttpResponse &response)
                {
                    applicationController->Statistics(
                        request,
                        response);
                });

            router.RegisterRoute(
                jobpilot::core::HttpMethod::kPut,
                "/api/v1/applications/{id}/status",
                [applicationController](
                    const jobpilot::core::HttpRequest &request,
                    jobpilot::core::HttpResponse &response)
                {
                    applicationController->UpdateStatus(
                        request,
                        response);
                });
        }

        if (internalJobImportController != nullptr)
        {
            router.RegisterRoute(
                jobpilot::core::HttpMethod::kPost,
                "/internal/jobs/import",
                [internalJobImportController](
                    const jobpilot::core::HttpRequest &request,
                    jobpilot::core::HttpResponse &response)
                {
                    internalJobImportController->Import(
                        request,
                        response);
                });
        }

        if (internalSourceSyncController != nullptr)
        {
            router.RegisterRoute(
                jobpilot::core::HttpMethod::kPost,
                "/internal/jobs/source-sync",
                [internalSourceSyncController](
                    const jobpilot::core::HttpRequest &request,
                    jobpilot::core::HttpResponse &response)
                {
                    internalSourceSyncController->Sync(
                        request,
                        response);
                });
        }

        if (aiController != nullptr)
        {
            router.RegisterRoute(
                jobpilot::core::HttpMethod::kPost,
                "/api/v1/ai/match",
                [aiController](
                    const jobpilot::core::HttpRequest &request,
                    jobpilot::core::HttpResponse &response)
                {
                    aiController->CreateMatch(
                        request,
                        response);
                });

            router.RegisterRoute(
                jobpilot::core::HttpMethod::kGet,
                "/api/v1/ai/tasks/{id}",
                [aiController](
                    const jobpilot::core::HttpRequest &request,
                    jobpilot::core::HttpResponse &response)
                {
                    aiController->GetTask(
                        request,
                        response);
                });
        }

        if (aiInternalController != nullptr)
        {
            router.RegisterRoute(
                jobpilot::core::HttpMethod::kPost,
                "/internal/ai/tasks/claim",
                [aiInternalController](
                    const jobpilot::core::HttpRequest &request,
                    jobpilot::core::HttpResponse &response)
                {
                    aiInternalController->Claim(
                        request,
                        response);
                });

            router.RegisterRoute(
                jobpilot::core::HttpMethod::kPost,
                "/internal/ai/tasks/{id}/complete",
                [aiInternalController](
                    const jobpilot::core::HttpRequest &request,
                    jobpilot::core::HttpResponse &response)
                {
                    aiInternalController->Complete(
                        request,
                        response);
                });

            router.RegisterRoute(
                jobpilot::core::HttpMethod::kPost,
                "/internal/ai/tasks/{id}/fail",
                [aiInternalController](
                    const jobpilot::core::HttpRequest &request,
                    jobpilot::core::HttpResponse &response)
                {
                    aiInternalController->Fail(
                        request,
                        response);
                });

            router.RegisterRoute(
                jobpilot::core::HttpMethod::kGet,
                "/internal/ai/tasks/{id}/input",
                [aiInternalController](
                    const jobpilot::core::HttpRequest &request,
                    jobpilot::core::HttpResponse &response)
                {
                    aiInternalController->Input(
                        request,
                        response);
                });
        }

        if (internalJobReviewController != nullptr)
        {
            // Static route first so /review/backfill never reaches {id}.
            router.RegisterRoute(
                jobpilot::core::HttpMethod::kGet,
                "/internal/jobs/review-queue",
                [internalJobReviewController](
                    const jobpilot::core::HttpRequest &request,
                    jobpilot::core::HttpResponse &response)
                {
                    internalJobReviewController->Queue(
                        request,
                        response);
                });

            router.RegisterRoute(
                jobpilot::core::HttpMethod::kPost,
                "/internal/jobs/review/backfill",
                [internalJobReviewController](
                    const jobpilot::core::HttpRequest &request,
                    jobpilot::core::HttpResponse &response)
                {
                    internalJobReviewController->Backfill(
                        request,
                        response);
                });

            router.RegisterRoute(
                jobpilot::core::HttpMethod::kPost,
                "/internal/jobs/review/{id}",
                [internalJobReviewController](
                    const jobpilot::core::HttpRequest &request,
                    jobpilot::core::HttpResponse &response)
                {
                    internalJobReviewController->Review(
                        request,
                        response);
                });
        }

        router.RegisterRoute(
            jobpilot::core::HttpMethod::kPost,
            "/hello",
            [](const jobpilot::core::HttpRequest &request,
               jobpilot::core::HttpResponse &response)
            {
                response.SetStatusCode(200);
                response.SetStatusMessage("OK");
                response.SetHeader(
                    "Content-Type",
                    "application/json");

                response.SetBody(
                    request.Body());

                response.SetKeepAlive(
                    request.KeepAlive());
            });

    }

} // namespace jobpilot::service
