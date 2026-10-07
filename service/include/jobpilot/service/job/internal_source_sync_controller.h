#pragma once

#include <string>

#include "jobpilot/http/http_request.h"
#include "jobpilot/http/http_response.h"
#include "jobpilot/service/job/source_sync_service.h"

namespace jobpilot::service
{
    class InternalSourceSyncController
    {
    public:
        InternalSourceSyncController(
            SourceSyncService &service,
            std::string internalToken);

        void Sync(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

    private:
        SourceSyncService &service_;
        std::string internalToken_;
    };

} // namespace jobpilot::service
