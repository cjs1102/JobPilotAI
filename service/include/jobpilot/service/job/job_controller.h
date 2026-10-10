#pragma once

#include "jobpilot/http/http_request.h"
#include "jobpilot/http/http_response.h"
#include "jobpilot/service/job/job_service.h"

namespace jobpilot::service
{
    class JobRankingService;

    class JobController
    {
    public:
        JobController(
            JobService &service,
            JobRankingService *rankingService = nullptr);

        void Search(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

        void Detail(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

        void Recommended(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

    private:
        JobService &service_;
        JobRankingService *rankingService_;

        // 0015A: public-visibility switch (JOB_PUBLIC_INCLUDE_NEEDS_REVIEW).
        // Enabled only for the compatibility rollback path; it never changes
        // stored data.
        bool includeNeedsReview_ = false;
    };

} // namespace jobpilot::service
