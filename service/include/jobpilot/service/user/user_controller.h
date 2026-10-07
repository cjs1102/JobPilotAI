#pragma once

#include "jobpilot/http/http_request.h"
#include "jobpilot/http/http_response.h"
#include "jobpilot/service/user/user_service.h"

namespace jobpilot::service
{

    class UserController
    {
    public:
        explicit UserController(UserService &service);

        void Register(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

        void Login(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

        void Me(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

        void GetProfile(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

        void UpdateProfile(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

        void ListSkills(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

        void CreateSkill(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

        void DeleteSkill(
            const jobpilot::core::HttpRequest &request,
            jobpilot::core::HttpResponse &response);

    private:
        UserService &service_;
    };

} // namespace jobpilot::service
