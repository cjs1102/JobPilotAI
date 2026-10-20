#pragma once

#include <cstddef>
#include <string>

namespace jobpilot::service
{
    // 0020: jobs.apply_url is VARCHAR(512), which MySQL counts in characters
    // (UTF-8 code points), not bytes. Keep the application cap in the same
    // unit so a valid 512-character URL is not rejected for its byte length.
    inline constexpr std::size_t kMaxApplyUrlCharacters = 512;

    inline bool IsAbsoluteHttpUrl(const std::string &value)
    {
        const bool https = value.rfind("https://", 0) == 0;
        const bool http = value.rfind("http://", 0) == 0;

        if (!https && !http)
        {
            return false;
        }

        const std::size_t schemeLength = https ? 8U : 7U;

        if (value.size() <= schemeLength)
        {
            return false;
        }

        // Require a non-empty authority/host. "http://" and "http:///path"
        // are not usable absolute URLs.
        const std::size_t hostEnd =
            value.find_first_of("/?#", schemeLength);

        if (hostEnd == schemeLength)
        {
            return false;
        }

        for (const unsigned char character : value)
        {
            // Raw control characters and spaces are not valid in an absolute
            // URL; the source must have percent-encoded them.
            if (character <= 0x20U)
            {
                return false;
            }
        }

        return true;
    }

    // Count UTF-8 code points. Continuation bytes (10xxxxxx) do not start a
    // character; invalid sequences are counted byte-by-byte, which is
    // conservative (it can only make the value too long, never too short).
    inline std::size_t Utf8CharacterCount(const std::string &value)
    {
        std::size_t count = 0;

        for (const unsigned char character : value)
        {
            if ((character & 0xC0U) != 0x80U)
            {
                ++count;
            }
        }

        return count;
    }

    inline bool IsValidApplyUrl(const std::string &value)
    {
        return IsAbsoluteHttpUrl(value) &&
               Utf8CharacterCount(value) <= kMaxApplyUrlCharacters;
    }
}
