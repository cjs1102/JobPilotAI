#include "jobpilot/http/http_request.h"
#include "jobpilot/http/http_request_parser.h"
#include "jobpilot/network/buffer.h"

#include <string>

#include <gtest/gtest.h>

namespace jobpilot::core
{
    TEST(HttpCorrectnessTest, HeaderLookupIsCaseInsensitive)
    {
        HttpRequest request;
        request.SetHeader("hOsT", "localhost");

        EXPECT_TRUE(request.HasHeader("HOST"));
        EXPECT_TRUE(request.HasHeader("host"));
        EXPECT_EQ(request.GetHeader("Host"), "localhost");
    }

    TEST(HttpCorrectnessTest, ParserAcceptsLowercaseFramingHeaders)
    {
        Buffer buffer;
        buffer.Append(
            "POST /hello HTTP/1.1\r\n"
            "hOsT: localhost\r\n"
            "cOnTeNt-LeNgTh: 7\r\n"
            "cOnNeCtIoN: close\r\n"
            "\r\n"
            "{\"a\":1}");

        HttpRequestParser parser;

        ASSERT_EQ(
            parser.Parse(&buffer),
            ParseResult::kComplete);

        EXPECT_EQ(parser.Request().Body(), "{\"a\":1}");
        EXPECT_FALSE(parser.Request().KeepAlive());
    }

    TEST(HttpCorrectnessTest, RejectsDuplicateContentLength)
    {
        Buffer buffer;
        buffer.Append(
            "POST /hello HTTP/1.1\r\n"
            "Content-Length: 7\r\n"
            "Content-Length: 7\r\n"
            "\r\n"
            "{\"a\":1}");

        HttpRequestParser parser;

        EXPECT_EQ(
            parser.Parse(&buffer),
            ParseResult::kError);
    }

    TEST(
        HttpCorrectnessTest,
        RejectsMixedCaseDuplicateContentLength)
    {
        Buffer buffer;
        buffer.Append(
            "POST /hello HTTP/1.1\r\n"
            "Content-Length: 7\r\n"
            "content-length: 7\r\n"
            "\r\n"
            "{\"a\":1}");

        HttpRequestParser parser;

        EXPECT_EQ(
            parser.Parse(&buffer),
            ParseResult::kError);
    }
}
