#pragma once

#include <cstddef>

#include "jobpilot/http/http_request.h"

namespace jobpilot::core
{

    class Buffer;

    enum class ParseResult
    {
        kIncomplete,
        kComplete,
        kError
    };

    enum class ParseState
    {
        kParseRequestLine,
        kParseHeaders,
        kParseBody,
        kParseComplete
    };

    class HttpRequestParser
    {
    public:
        HttpRequestParser();

        ParseResult Parse(Buffer *buffer);

        ParseState State() const;

        const HttpRequest &Request() const;

        void Reset();

    private:
        ParseResult ParseRequestLine(Buffer *buffer);
        ParseResult ParseHeaders(Buffer *buffer);
        ParseResult ParseBody(Buffer *buffer);

        ParseState state_;
        HttpRequest request_;
        std::size_t contentLength_;
    };

} // namespace jobpilot::core