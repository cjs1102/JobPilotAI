#include "jobpilot/core/utf8.h"

#include <cstddef>

namespace jobpilot::core
{
    namespace
    {
        bool IsContinuation(unsigned char value) noexcept
        {
            return (value & 0xC0U) == 0x80U;
        }
    } // namespace

    bool IsValidUtf8(std::string_view bytes) noexcept
    {
        std::size_t index = 0;

        while (index < bytes.size())
        {
            const unsigned char first =
                static_cast<unsigned char>(bytes[index]);

            if (first <= 0x7FU)
            {
                ++index;
                continue;
            }

            if (first >= 0xC2U && first <= 0xDFU)
            {
                if (index + 1 >= bytes.size() ||
                    !IsContinuation(
                        static_cast<unsigned char>(bytes[index + 1])))
                {
                    return false;
                }

                index += 2;
                continue;
            }

            if (first >= 0xE0U && first <= 0xEFU)
            {
                if (index + 2 >= bytes.size())
                {
                    return false;
                }

                const unsigned char second =
                    static_cast<unsigned char>(bytes[index + 1]);
                const unsigned char third =
                    static_cast<unsigned char>(bytes[index + 2]);

                if (!IsContinuation(third))
                {
                    return false;
                }

                if (first == 0xE0U)
                {
                    // E0 requires A0..BF to avoid an overlong 3-byte form.
                    if (second < 0xA0U || second > 0xBFU)
                    {
                        return false;
                    }
                }
                else if (first == 0xEDU)
                {
                    // ED 80..9F only; A0..BF would encode a surrogate.
                    if (second < 0x80U || second > 0x9FU)
                    {
                        return false;
                    }
                }
                else if (!IsContinuation(second))
                {
                    return false;
                }

                index += 3;
                continue;
            }

            if (first >= 0xF0U && first <= 0xF4U)
            {
                if (index + 3 >= bytes.size())
                {
                    return false;
                }

                const unsigned char second =
                    static_cast<unsigned char>(bytes[index + 1]);
                const unsigned char third =
                    static_cast<unsigned char>(bytes[index + 2]);
                const unsigned char fourth =
                    static_cast<unsigned char>(bytes[index + 3]);

                if (!IsContinuation(third) || !IsContinuation(fourth))
                {
                    return false;
                }

                if (first == 0xF0U)
                {
                    // F0 requires 90..BF to avoid an overlong 4-byte form.
                    if (second < 0x90U || second > 0xBFU)
                    {
                        return false;
                    }
                }
                else if (first == 0xF4U)
                {
                    // F4 80..8F only; 90..BF would exceed U+10FFFF.
                    if (second < 0x80U || second > 0x8FU)
                    {
                        return false;
                    }
                }
                else if (!IsContinuation(second))
                {
                    return false;
                }

                index += 4;
                continue;
            }

            return false;
        }

        return true;
    }
} // namespace jobpilot::core
