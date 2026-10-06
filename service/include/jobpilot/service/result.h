#pragma once

#include "jobpilot/error.h"

namespace jobpilot::service
{

    template <typename T>
    struct ServiceResult
    {
        ErrorCode code = ErrorCode::kInternalError;
        T value{};
    };

} // namespace jobpilot::service
