#pragma once

namespace jobpilot::test
{
    // Test-only HTTP client receive timeout. Sanitizer builds are slower
    // (bcrypt in particular), so allow more wall-clock time without changing
    // any assertion. Keep the backend copy in sync.
    constexpr int HttpTestTimeoutSeconds(int normal_seconds)
    {
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__) || \
    defined(__SANITIZE_UNDEFINED__)
        return normal_seconds * 5;
#else
        return normal_seconds;
#endif
    }

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__) || \
    defined(__SANITIZE_UNDEFINED__)
    static_assert(HttpTestTimeoutSeconds(3) == 15);
    static_assert(HttpTestTimeoutSeconds(2) == 10);
#else
    static_assert(HttpTestTimeoutSeconds(3) == 3);
    static_assert(HttpTestTimeoutSeconds(2) == 2);
#endif
}
