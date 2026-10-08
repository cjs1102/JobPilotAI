#pragma once

#include <string>

#include "jobpilot/service/ai/match_result.h"

namespace jobpilot::service
{
    // Deterministic JSON encoding of the match_rule_v1 result contract
    // (ExecPlan 0013 section 5.5). Object keys follow the documented order,
    // breakdown components are omitted when absent, and numbers keep their
    // full precision so a parse round-trip returns the same values.
    std::string SerializeMatchResult(
        const MatchResult &result);

} // namespace jobpilot::service
