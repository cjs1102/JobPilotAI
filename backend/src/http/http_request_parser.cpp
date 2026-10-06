#include "jobpilot/http/http_request_parser.h"

#include <cctype>
#include <cstddef>
#include <stdexcept>
#include <string>

#include "jobpilot/network/buffer.h"

namespace jobpilot::core
{

    namespace
    {

        std::size_t FindCrlf(const Buffer *buffer)
        {
            const char *data = buffer->Peek();
            const std::size_t length = buffer->ReadableBytes();

            for (std::size_t i = 0; i + 1 < length; ++i)
            {
                if (data[i] == '\r' && data[i + 1] == '\n')
                {
                    return i;
                }
            }

            return std::string::npos;
        }

        std::string Trim(const std::string &value)
        {
            std::size_t begin = 0;
            std::size_t end = value.size();

            while (begin < end &&
                   std::isspace(
                       static_cast<unsigned char>(value[begin])))
            {
                ++begin;
            }

            while (end > begin &&
                   std::isspace(
                       static_cast<unsigned char>(value[end - 1])))
            {
                --end;
            }

            return value.substr(begin, end - begin);
        }

        std::string ToLower(std::string value)
        {
            for (char &character : value)
            {
                character = static_cast<char>(
                    std::tolower(
                        static_cast<unsigned char>(character)));
            }

            return value;
        }

        bool ParseContentLength(
            const std::string &value,
            std::size_t *contentLength)
        {
            if (contentLength == nullptr || value.empty())
            {
                return false;
            }

            std::size_t result = 0;

            for (char character : value)
            {
                if (character < '0' || character > '9')
                {
                    return false;
                }

                const std::size_t digit =
                    static_cast<std::size_t>(character - '0');

                if (result >
                    (static_cast<std::size_t>(-1) - digit) / 10)
                {
                    return false;
                }

                result = result * 10 + digit;
            }

            *contentLength = result;
            return true;
        }

    } // namespace

    HttpRequestParser::HttpRequestParser()
        : state_(ParseState::kParseRequestLine),
          request_(),
          contentLength_(0),
          hasContentLength_(false)
    {
    }

    ParseResult HttpRequestParser::Parse(Buffer *buffer)
    {
        if (buffer == nullptr)
        {
            return ParseResult::kError;
        }

        while (true)
        {
            ParseResult result = ParseResult::kIncomplete;

            switch (state_)
            {
            case ParseState::kParseRequestLine:
                result = ParseRequestLine(buffer);
                break;

            case ParseState::kParseHeaders:
                result = ParseHeaders(buffer);
                break;

            case ParseState::kParseBody:
                result = ParseBody(buffer);
                break;

            case ParseState::kParseComplete:
                return ParseResult::kComplete;
            }

            if (result == ParseResult::kIncomplete ||
                result == ParseResult::kError)
            {
                return result;
            }

            if (state_ == ParseState::kParseComplete)
            {
                return ParseResult::kComplete;
            }
        }
    }

    ParseResult HttpRequestParser::ParseRequestLine(
        Buffer *buffer)
    {
        const std::size_t position = FindCrlf(buffer);

        if (position == std::string::npos)
        {
            return ParseResult::kIncomplete;
        }

        if (position == 0)
        {
            return ParseResult::kError;
        }

        const std::string line(
            buffer->Peek(),
            position);

        const std::size_t firstSpace =
            line.find(' ');

        if (firstSpace == std::string::npos)
        {
            return ParseResult::kError;
        }

        const std::size_t secondSpace =
            line.find(' ', firstSpace + 1);

        if (secondSpace == std::string::npos ||
            secondSpace == firstSpace + 1)
        {
            return ParseResult::kError;
        }

        const std::string method =
            line.substr(0, firstSpace);

        const std::string path =
            line.substr(
                firstSpace + 1,
                secondSpace - firstSpace - 1);

        const std::string version =
            line.substr(secondSpace + 1);

        if (method == "GET")
        {
            request_.SetMethod(HttpMethod::kGet);
        }
        else if (method == "POST")
        {
            request_.SetMethod(HttpMethod::kPost);
        }
        else
        {
            return ParseResult::kError;
        }

        if (path.empty())
        {
            return ParseResult::kError;
        }

        if (version != "HTTP/1.1")
        {
            return ParseResult::kError;
        }

        request_.SetPath(path);
        request_.SetVersion(HttpVersion::kHttp11);

        buffer->Retrieve(position + 2);

        state_ = ParseState::kParseHeaders;

        return ParseResult::kComplete;
    }

    ParseResult HttpRequestParser::ParseHeaders(
        Buffer *buffer)
    {
        while (true)
        {
            const std::size_t position = FindCrlf(buffer);

            if (position == std::string::npos)
            {
                return ParseResult::kIncomplete;
            }

            // 空行：Header 区域结束。
            if (position == 0)
            {
                buffer->Retrieve(2);

                contentLength_ = 0;

                if (request_.HasHeader("Content-Length"))
                {
                    const std::string contentLength =
                        Trim(request_.GetHeader("Content-Length"));

                    if (!ParseContentLength(
                            contentLength,
                            &contentLength_))
                    {
                        return ParseResult::kError;
                    }
                }
                else
                {
                    // 当前项目的最小 HTTP 子集：
                    // POST 必须通过 Content-Length 确定 Body 边界。
                    if (request_.Method() == HttpMethod::kPost)
                    {
                        return ParseResult::kError;
                    }
                }

                if (request_.HasHeader("Connection"))
                {
                    const std::string connection =
                        ToLower(
                            Trim(
                                request_.GetHeader("Connection")));

                    if (connection == "close")
                    {
                        request_.SetKeepAlive(false);
                    }
                    else if (connection == "keep-alive")
                    {
                        request_.SetKeepAlive(true);
                    }
                }
                else
                {
                    // HTTP/1.1 默认保持连接。
                    request_.SetKeepAlive(true);
                }

                if (contentLength_ == 0)
                {
                    state_ = ParseState::kParseComplete;
                    return ParseResult::kComplete;
                }

                state_ = ParseState::kParseBody;
                return ParseResult::kComplete;
            }

            const std::string line(
                buffer->Peek(),
                position);

            const std::size_t colon =
                line.find(':');

            if (colon == std::string::npos ||
                colon == 0)
            {
                return ParseResult::kError;
            }

            const std::string name =
                Trim(line.substr(0, colon));

            const std::string value =
                Trim(line.substr(colon + 1));

            if (name.empty())
            {
                return ParseResult::kError;
            }

            const std::string lowerName =
                ToLower(name);

            if (lowerName == "content-length")
            {
                if (hasContentLength_)
                {
                    return ParseResult::kError;
                }

                hasContentLength_ = true;
            }

            request_.SetHeader(name, value);

            buffer->Retrieve(position + 2);

            // Content-Length / Connection 在真正结束 Header
            // 时统一解释，因此这里只保存 Header。
        }
    }

    ParseResult HttpRequestParser::ParseBody(
        Buffer *buffer)
    {
        if (contentLength_ == 0)
        {
            state_ = ParseState::kParseComplete;
            return ParseResult::kComplete;
        }

        if (buffer->ReadableBytes() < contentLength_)
        {
            return ParseResult::kIncomplete;
        }

        const std::string body(
            buffer->Peek(),
            contentLength_);

        request_.SetBody(body);

        buffer->Retrieve(contentLength_);

        state_ = ParseState::kParseComplete;

        return ParseResult::kComplete;
    }

    ParseState HttpRequestParser::State() const
    {
        return state_;
    }

    const HttpRequest &HttpRequestParser::Request() const
    {
        return request_;
    }

    void HttpRequestParser::Reset()
    {
        state_ = ParseState::kParseRequestLine;
        request_ = HttpRequest();
        contentLength_ = 0;
        hasContentLength_ = false;
    }

} // namespace jobpilot::core
