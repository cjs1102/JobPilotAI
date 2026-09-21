#include <cassert>
#include <iostream>
#include <string>

#include "jobpilot/http/http_response.h"

using namespace jobpilot::core;

int main()
{
    HttpResponse response;

    // 默认状态
    assert(response.StatusCode() == 200);
    assert(response.StatusMessage() == "OK");
    assert(response.Body().empty());
    assert(response.KeepAlive());

    // 设置响应
    response.SetStatusCode(200);
    response.SetStatusMessage("OK");

    response.SetHeader(
        "Content-Type",
        "application/json");

    response.SetBody(
        R"({"code":0,"message":"success"})");

    response.SetKeepAlive(true);

    // Getter
    assert(response.StatusCode() == 200);
    assert(response.StatusMessage() == "OK");

    assert(response.HasHeader("Content-Type"));
    assert(response.GetHeader("Content-Type") ==
           "application/json");

    assert(response.Body() ==
           R"({"code":0,"message":"success"})");

    assert(response.KeepAlive());

    // Serialize
    const std::string serialized = response.Serialize();

    assert(
        serialized.find("HTTP/1.1 200 OK\r\n") !=
        std::string::npos);

    assert(
        serialized.find(
            "Content-Type: application/json\r\n") !=
        std::string::npos);

    assert(
        serialized.find(
            "Content-Length: 30\r\n") !=
        std::string::npos);

    assert(
        serialized.find(
            "Connection: keep-alive\r\n") !=
        std::string::npos);

    assert(
        serialized.find(
            "\r\n\r\n"
            R"({"code":0,"message":"success"})") !=
        std::string::npos);

    // close 模式
    response.SetKeepAlive(false);

    const std::string closeSerialized =
        response.Serialize();

    assert(
        closeSerialized.find(
            "Connection: close\r\n") !=
        std::string::npos);

    std::cout
        << "http response test passed"
        << std::endl;

    return 0;
}