#include "jobpilot/core/logger.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <stdexcept>

#include <spdlog/sinks/stdout_color_sinks.h>

namespace jobpilot::core
{
    namespace
    {
        std::mutex loggerMutex;
        std::shared_ptr<spdlog::logger> applicationLogger;

        std::string ToLower(std::string value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](unsigned char character)
                {
                    return static_cast<char>(
                        std::tolower(character));
                });

            return value;
        }

        spdlog::level::level_enum ParseLevel(
            const std::string &level)
        {
            const std::string normalized =
                ToLower(level);

            if (normalized == "trace")
            {
                return spdlog::level::trace;
            }

            if (normalized == "debug")
            {
                return spdlog::level::debug;
            }

            if (normalized == "info")
            {
                return spdlog::level::info;
            }

            if (normalized == "warn" ||
                normalized == "warning")
            {
                return spdlog::level::warn;
            }

            if (normalized == "error")
            {
                return spdlog::level::err;
            }

            if (normalized == "critical")
            {
                return spdlog::level::critical;
            }

            if (normalized == "off")
            {
                return spdlog::level::off;
            }

            throw std::invalid_argument(
                "unsupported log level: " + level);
        }
    } // namespace

    void Logger::Initialize(
        const std::string &level,
        SinkPtr sink)
    {
        const spdlog::level::level_enum parsedLevel =
            ParseLevel(level);

        if (sink == nullptr)
        {
            sink =
                std::make_shared<
                    spdlog::sinks::stdout_color_sink_mt>();
        }

        auto logger =
            std::make_shared<spdlog::logger>(
                "jobpilot",
                std::move(sink));

        logger->set_level(parsedLevel);
        logger->set_pattern(
            "[%Y-%m-%d %H:%M:%S.%e] "
            "[%^%l%$] [%t] %v");
        logger->flush_on(spdlog::level::warn);

        {
            std::lock_guard<std::mutex> lock(
                loggerMutex);

            applicationLogger = logger;
            spdlog::set_default_logger(
                applicationLogger);
        }
    }

    std::shared_ptr<spdlog::logger> Logger::Get()
    {
        std::lock_guard<std::mutex> lock(
            loggerMutex);

        if (applicationLogger)
        {
            return applicationLogger;
        }

        return spdlog::default_logger();
    }

    void Logger::Shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(
                loggerMutex);

            applicationLogger.reset();
        }

        spdlog::shutdown();
    }

} // namespace jobpilot::core
