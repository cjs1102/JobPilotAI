#pragma once

#include <string_view>

namespace jobpilot::core
{
    // Strict UTF-8 check used at request boundaries before a value can reach
    // storage or MySQL. Rejects overlong forms, surrogate code points, values
    // above U+10FFFF, truncated sequences, and stray continuation bytes.
    bool IsValidUtf8(std::string_view bytes) noexcept;
} // namespace jobpilot::core
