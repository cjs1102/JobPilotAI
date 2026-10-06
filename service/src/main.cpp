#include "jobpilot/core/config.h"
#include "jobpilot/core/logger.h"
#include "jobpilot/http/http_server.h"
#include "jobpilot/network/event_loop.h"
#include "jobpilot/service/router.h"

#include <exception>
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

        jobpilot::core::EventLoop loop;

        jobpilot::core::HttpServer server(
            &loop,
            config.server.host,
            config.server.port);

        jobpilot::service::RegisterRoutes(&server);

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
