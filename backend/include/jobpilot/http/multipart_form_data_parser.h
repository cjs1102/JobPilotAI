#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace jobpilot::core
{
    enum class MultipartParseResult
    {
        kSuccess,
        kMalformed,
        kUnsupportedContentType,
        kLimitExceeded
    };

    struct MultipartPart
    {
        std::string name;
        std::optional<std::string> filename;
        std::optional<std::string> contentType;
        std::string_view body;
    };

    struct MultipartFormData
    {
        std::vector<MultipartPart> parts;
    };

    // Part bodies are views into the supplied request body. The caller must
    // keep that storage alive for as long as the parsed form is used.
    class MultipartFormDataParser
    {
    public:
        static MultipartParseResult Parse(
            std::string_view contentType,
            std::string_view body,
            MultipartFormData *form);
    };

} // namespace jobpilot::core
