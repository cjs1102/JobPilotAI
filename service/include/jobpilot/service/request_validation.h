#pragma once

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "jobpilot/core/utf8.h"

namespace jobpilot::service
{
    // Additive parameter-error payload. The code/message/status stay unchanged;
    // clients that only inspect code/message can ignore data.
    inline nlohmann::json ParamError(std::string_view field)
    {
        return nlohmann::json{{"field", std::string(field)}};
    }

    // Reject any invalid UTF-8 in JSON keys or string values before a request
    // reaches storage/MySQL. `field` receives the last object key (or "body"
    // for a top-level string), which is enough for the public write paths.
    inline bool ValidateJsonUtf8(
        const nlohmann::json &value,
        std::string *field,
        std::string_view current = {})
    {
        if (value.is_string())
        {
            if (!jobpilot::core::IsValidUtf8(
                    value.get_ref<const std::string &>()))
            {
                if (field != nullptr)
                {
                    *field = current.empty()
                                 ? "body"
                                 : std::string(current);
                }

                return false;
            }

            return true;
        }

        if (value.is_object())
        {
            for (auto iterator = value.begin();
                 iterator != value.end();
                 ++iterator)
            {
                if (!jobpilot::core::IsValidUtf8(iterator.key()))
                {
                    if (field != nullptr)
                    {
                        *field = iterator.key();
                    }

                    return false;
                }

                if (!ValidateJsonUtf8(
                        iterator.value(),
                        field,
                        iterator.key()))
                {
                    return false;
                }
            }

            return true;
        }

        if (value.is_array())
        {
            for (const auto &item : value)
            {
                if (!ValidateJsonUtf8(item, field, current))
                {
                    return false;
                }
            }
        }

        return true;
    }
} // namespace jobpilot::service
