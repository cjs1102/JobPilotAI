#pragma once

#include <string>

#include "jobpilot/http/http_request.h"
#include "jobpilot/http/http_response.h"
#include "jobpilot/service/job/job_review.h"

namespace jobpilot::service
{
    // Protected internal review endpoints (ExecPlan 0015A sections 5.2/5.4.2).
    // Access control reuses the existing constant-time X-Internal-Token
    // mechanism only; `reviewer` is an audit label, not an authenticated
    // identity, and the token is never logged.
    class InternalJobReviewController
    {
    public:
        InternalJobReviewController(
            JobReviewService &service,
            std::string internalToken);

        // GET /internal/jobs/review-queue?status=&page=&page_size=
        void Queue(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

        // POST /internal/jobs/review/{job_id}
        void Review(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

        // POST /internal/jobs/review/backfill?mode=dry_run|apply|revert
        void Backfill(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

    private:
        JobReviewService &service_;
        std::string internalToken_;
    };

} // namespace jobpilot::service
