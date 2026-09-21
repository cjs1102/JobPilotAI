#include <cassert>
#include <iostream>
#include <string>

#include "jobpilot/http/http_request_parser.h"
#include "jobpilot/network/buffer.h"

using namespace jobpilot::core;

int main()
{
    // ------------------------------------------------------------
    // 1. 正常 GET 请求
    // ------------------------------------------------------------
    {
        Buffer buffer;

        buffer.Append(
            "GET /hello HTTP/1.1\r\n"
            "Host: localhost:8080\r\n"
            "\r\n");

        HttpRequestParser parser;

        const ParseResult result = parser.Parse(&buffer);

        assert(result == ParseResult::kComplete);
        assert(parser.State() == ParseState::kParseComplete);

        const HttpRequest &request = parser.Request();

        assert(request.Method() == HttpMethod::kGet);
        assert(request.Path() == "/hello");
        assert(request.Version() == HttpVersion::kHttp11);
        assert(request.GetHeader("Host") == "localhost:8080");
        assert(request.KeepAlive());

        assert(buffer.ReadableBytes() == 0);
    }

    // ------------------------------------------------------------
    // 2. POST + JSON Body
    // ------------------------------------------------------------
    {
        Buffer buffer;

        buffer.Append(
            "POST /hello HTTP/1.1\r\n"
            "Host: localhost:8080\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: 7\r\n"
            "Connection: keep-alive\r\n"
            "\r\n"
            "{\"a\":1}");

        HttpRequestParser parser;

        const ParseResult result = parser.Parse(&buffer);

        assert(result == ParseResult::kComplete);
        assert(parser.State() == ParseState::kParseComplete);

        const HttpRequest &request = parser.Request();

        assert(request.Method() == HttpMethod::kPost);
        assert(request.Path() == "/hello");
        assert(request.Version() == HttpVersion::kHttp11);

        assert(request.GetHeader("Content-Type") ==
               "application/json");

        assert(request.GetHeader("Content-Length") == "7");

        assert(request.Body() == "{\"a\":1}");

        assert(request.KeepAlive());

        assert(buffer.ReadableBytes() == 0);
    }

    // ------------------------------------------------------------
    // 3. 半包：分三次追加数据
    // ------------------------------------------------------------
    {
        Buffer buffer;
        HttpRequestParser parser;

        buffer.Append(
            "POST /hello HTTP/1.1\r\n"
            "Host: localhost:8080\r\n"
            "Content-L");

        ParseResult result = parser.Parse(&buffer);

        assert(result == ParseResult::kIncomplete);
        assert(parser.State() ==
               ParseState::kParseHeaders);

        buffer.Append(
            "ength: 7\r\n"
            "\r\n"
            "{\"a\":");

        result = parser.Parse(&buffer);

        assert(result == ParseResult::kIncomplete);
        assert(parser.State() ==
               ParseState::kParseBody);

        buffer.Append("1}");

        result = parser.Parse(&buffer);

        assert(result == ParseResult::kComplete);
        assert(parser.State() ==
               ParseState::kParseComplete);

        assert(parser.Request().Method() ==
               HttpMethod::kPost);

        assert(parser.Request().Path() ==
               "/hello");

        assert(parser.Request().Body() ==
               "{\"a\":1}");

        assert(buffer.ReadableBytes() == 0);
    }

    // ------------------------------------------------------------
    // 4. 粘包：两个 HTTP 请求一次进入 Buffer
    // ------------------------------------------------------------
    {
        Buffer buffer;

        const std::string request1 =
            "GET /hello HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "\r\n";

        const std::string request2 =
            "GET /health HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "\r\n";

        buffer.Append(request1);
        buffer.Append(request2);

        HttpRequestParser parser;

        ParseResult result = parser.Parse(&buffer);

        assert(result == ParseResult::kComplete);

        assert(parser.Request().Path() == "/hello");

        // 第一个请求解析完成后，
        // 第二个请求必须仍然留在 Buffer 中。
        assert(buffer.ReadableBytes() ==
               request2.size());

        parser.Reset();

        result = parser.Parse(&buffer);

        assert(result == ParseResult::kComplete);

        assert(parser.Request().Path() == "/health");

        assert(buffer.ReadableBytes() == 0);
    }

    // ------------------------------------------------------------
    // 5. 非法请求
    // ------------------------------------------------------------
    {
        Buffer buffer;

        buffer.Append(
            "BAD /hello HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "\r\n");

        HttpRequestParser parser;

        const ParseResult result = parser.Parse(&buffer);

        assert(result == ParseResult::kError);
    }

    std::cout << "http request parser test passed"
              << std::endl;

    return 0;
}