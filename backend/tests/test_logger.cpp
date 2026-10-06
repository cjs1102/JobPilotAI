#include "jobpilot/core/logger.h"

#include <memory>
#include <sstream>
#include <stdexcept>

#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>

namespace jobpilot::core
{
    TEST(LoggerTest, InitializesAndWrites)
    {
        std::ostringstream output;
        Logger::Initialize(
            "info",
            std::make_shared<spdlog::sinks::ostream_sink_mt>(
                output));

        Logger::Get()->info("logger smoke message");
        Logger::Get()->flush();

        EXPECT_NE(
            output.str().find("logger smoke message"),
            std::string::npos);

        Logger::Shutdown();
    }

    TEST(LoggerTest, RejectsUnknownLevel)
    {
        std::ostringstream output;

        EXPECT_THROW(
            Logger::Initialize(
                "verbose",
                std::make_shared<spdlog::sinks::ostream_sink_mt>(
                    output)),
            std::invalid_argument);

        Logger::Shutdown();
    }

} // namespace jobpilot::core
