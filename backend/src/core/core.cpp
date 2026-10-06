#include "jobpilot/core/logger.h"

namespace jobpilot
{

    void core_init()
    {
        core::Logger::Get()->info("JobPilot Core initialized");
    }

}
