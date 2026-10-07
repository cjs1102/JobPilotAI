#include "jobpilot/core/config.h"
#include "jobpilot/core/logger.h"
#include "jobpilot/cache/redis_client.h"
#include "jobpilot/cache/session_manager.h"
#include "jobpilot/database/mysql_pool.h"
#include "jobpilot/http/http_server.h"
#include "jobpilot/network/event_loop.h"
#include "jobpilot/storage/file_storage.h"
#include "jobpilot/service/application/application_controller.h"
#include "jobpilot/service/application/application_dao.h"
#include "jobpilot/service/application/application_service.h"
#include "jobpilot/service/auth/session_authenticator.h"
#include "jobpilot/service/job/job_controller.h"
#include "jobpilot/service/job/job_dao.h"
#include "jobpilot/service/job/job_ingestion_dao.h"
#include "jobpilot/service/job/job_ingestion_service.h"
#include "jobpilot/service/job/job_ranking_service.h"
#include "jobpilot/service/job/job_service.h"
#include "jobpilot/service/job/internal_job_import_controller.h"
#include "jobpilot/service/resume/resume_controller.h"
#include "jobpilot/service/resume/resume_dao.h"
#include "jobpilot/service/resume/resume_service.h"
#include "jobpilot/service/router.h"
#include "jobpilot/service/user/user_controller.h"
#include "jobpilot/service/user/user_dao.h"
#include "jobpilot/service/user/user_service.h"

#include <exception>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
    std::string ParseConfigPath(
        int argc,
        char *argv[])
    {
        std::string path = "config/config.yaml";

        for (int index = 1; index < argc; ++index)
        {
            const std::string argument = argv[index];

            constexpr const char *kConfigPrefix = "--config=";

            if (argument.rfind(kConfigPrefix, 0) == 0)
            {
                path = argument.substr(
                    std::string(kConfigPrefix).size());

                if (path.empty())
                {
                    throw std::invalid_argument(
                        "--config requires a file path");
                }

                continue;
            }

            throw std::invalid_argument(
                "unknown argument: " + argument);
        }

        return path;
    }
} // namespace

int main(int argc, char *argv[])
{
    auto logger = jobpilot::core::Logger::Get();

    try
    {
        const std::string configPath =
            ParseConfigPath(argc, argv);

        const jobpilot::core::Config config =
            jobpilot::core::Config::LoadFromFile(
                configPath,
                jobpilot::core::Config::CurrentEnvironment());

        jobpilot::core::Logger::Initialize(
            config.log.level);
        logger = jobpilot::core::Logger::Get();

        logger->info(
            "configuration loaded from {}",
            configPath);
        logger->info(
            "MySQL configuration: host={} port={} database={} user={} max_conn={}",
            config.mysql.host,
            config.mysql.port,
            config.mysql.database,
            config.mysql.user,
            config.mysql.maxConn);
        logger->info(
            "Redis configuration: host={} port={} max_conn={} timeout_ms={}",
            config.redis.host,
            config.redis.port,
            config.redis.maxConn,
            config.redis.timeoutMs);

        jobpilot::core::ConnectionPool mysqlPool(
            config.mysql.host,
            config.mysql.port,
            config.mysql.user,
            config.mysql.password,
            config.mysql.database,
            config.mysql.maxConn);

        jobpilot::core::RedisClient redisClient(
            config.redis.host,
            config.redis.port,
            config.redis.maxConn,
            config.redis.timeoutMs);

        jobpilot::core::SessionManager sessions(
            redisClient);

        jobpilot::service::SessionAuthenticator authenticator(
            sessions);

        std::unique_ptr<jobpilot::core::FileStorage> resumeStorage =
            jobpilot::core::MakeLocalFileStorage(
                config.storage.root);

        jobpilot::service::UserDao userDao(
            mysqlPool);

        jobpilot::service::UserService userService(
            userDao,
            sessions);

        jobpilot::service::UserController userController(
            userService);

        jobpilot::service::ResumeDao resumeDao(
            mysqlPool);

        jobpilot::service::ResumeService resumeService(
            resumeDao,
            mysqlPool,
            *resumeStorage,
            authenticator,
            config.storage.maxFileBytes);

        jobpilot::service::ResumeController resumeController(
            resumeService);

        jobpilot::service::JobDao jobDao(
            mysqlPool);

        jobpilot::service::JobService jobService(
            jobDao);

        jobpilot::service::JobRankingService jobRankingService(
            jobDao,
            userDao,
            authenticator);

        jobpilot::service::JobController jobController(
            jobService,
            &jobRankingService);

        jobpilot::service::JobIngestionDao jobIngestionDao;

        jobpilot::service::JobIngestionService jobIngestionService(
            jobIngestionDao,
            mysqlPool);

        jobpilot::service::InternalJobImportController
            internalJobImportController(
                jobIngestionService,
                config.internalApi.token);

        jobpilot::service::ApplicationDao applicationDao(
            mysqlPool);

        jobpilot::service::ApplicationService applicationService(
            applicationDao,
            jobDao,
            resumeDao,
            mysqlPool,
            authenticator);

        jobpilot::service::ApplicationController applicationController(
            applicationService);

        jobpilot::core::EventLoop loop;

        jobpilot::core::HttpServer server(
            &loop,
            config.server.host,
            config.server.port);

        jobpilot::service::RegisterRoutes(
            &server,
            &userController,
            &resumeController,
            &jobController,
            &applicationController,
            config.internalApi.token.empty()
                ? nullptr
                : &internalJobImportController);

        server.Start();

        logger->info(
            "HTTP server started on {}:{}",
            config.server.host,
            config.server.port);

        loop.Loop();

        server.Stop();

        logger->info("HTTP server stopped");
        jobpilot::core::Logger::Shutdown();

        return 0;
    }
    catch (const std::exception &error)
    {
        logger->critical(
            "service startup failed: {}",
            error.what());

        jobpilot::core::Logger::Shutdown();

        return 1;
    }
}
