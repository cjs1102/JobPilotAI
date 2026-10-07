#pragma once

#include <string>

#include "jobpilot/http/http_request.h"
#include "jobpilot/http/http_response.h"
#include "jobpilot/service/job/job_ingestion_service.h"

namespace jobpilot::service
{
    class InternalJobImportController
    {
    public:
        InternalJobImportController(
            JobIngestionService &service,
            std::string internalToken);

        void Import(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

    private:
        JobIngestionService &service_;
        std::string internalToken_;
    };

} // namespace jobpilot::service
