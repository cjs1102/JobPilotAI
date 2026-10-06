#pragma once

#include <memory>
#include <string>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/sink.h>

namespace jobpilot::core
{

    class Logger
    {
    public:
        using SinkPtr =
            std::shared_ptr<spdlog::sinks::sink>;

        static void Initialize(
            const std::string &level,
            SinkPtr sink = nullptr);

        static std::shared_ptr<spdlog::logger> Get();

        static void Shutdown();
    };

} // namespace jobpilot::core
