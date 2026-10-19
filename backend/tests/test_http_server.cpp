#include <arpa/inet.h>

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include "jobpilot/http/http_server.h"
#include "jobpilot/network/event_loop.h"
#include "http_test_timeout.h"

namespace jobpilot::core
{

namespace
{

constexpr uint16_t kTestPort = 19091;

struct ServerState
{
    std::mutex mutex;
    std::condition_variable condition;
    EventLoop *loop = nullptr;
    bool ready = false;
    std::exception_ptr exception;
};

bool SendAll(
    int socketFd,
    const std::string &data)
{
    std::size_t sent = 0;

    while (sent < data.size())
    {
        const ssize_t result =
            send(
                socketFd,
                data.data() + sent,
                data.size() - sent,
                0);

        if (result <= 0)
        {
            return false;
        }

        sent += static_cast<std::size_t>(result);
    }

    return true;
}

int CreateClientSocket()
{
    const int socketFd =
        socket(
            AF_INET,
            SOCK_STREAM,
            0);

    if (socketFd < 0)
    {
        return -1;
    }

    timeval timeout{};
    timeout.tv_sec = jobpilot::test::HttpTestTimeoutSeconds(2);
    timeout.tv_usec = 0;

    if (setsockopt(
            socketFd,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &timeout,
            sizeof(timeout)) != 0)
    {
        close(socketFd);
        return -1;
    }

    return socketFd;
}

bool ConnectToServer(int socketFd)
{
    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(kTestPort);

    const int converted =
        inet_pton(
            AF_INET,
            "127.0.0.1",
            &serverAddress.sin_addr);

    if (converted != 1)
    {
        return false;
    }

    return connect(
               socketFd,
               reinterpret_cast<sockaddr *>(&serverAddress),
               sizeof(serverAddress)) == 0;
}

bool ReceiveHttpResponse(
    int socketFd,
    std::string *receiveBuffer,
    std::string *response)
{
    if (receiveBuffer == nullptr ||
        response == nullptr)
    {
        return false;
    }

    const std::string headerDelimiter =
        "\r\n\r\n";

    char buffer[4096];

    while (receiveBuffer->find(headerDelimiter) ==
           std::string::npos)
    {
        const ssize_t received =
            recv(
                socketFd,
                buffer,
                sizeof(buffer),
                0);

        if (received <= 0)
        {
            return false;
        }

        receiveBuffer->append(
            buffer,
            static_cast<std::size_t>(received));
    }

    const std::size_t headerEnd =
        receiveBuffer->find(headerDelimiter);

    const std::string headers =
        receiveBuffer->substr(
            0,
            headerEnd);

    const std::string key =
        "Content-Length:";

    const std::size_t contentLengthPos =
        headers.find(key);

    if (contentLengthPos == std::string::npos)
    {
        return false;
    }

    const std::size_t valueStart =
        contentLengthPos + key.size();

    const std::size_t lineEnd =
        headers.find(
            "\r\n",
            valueStart);

    const std::string value =
        headers.substr(
            valueStart,
            lineEnd == std::string::npos
                ? std::string::npos
                : lineEnd - valueStart);

    std::size_t contentLength = 0;

    try
    {
        contentLength =
            static_cast<std::size_t>(
                std::stoull(value));
    }
    catch (...)
    {
        return false;
    }

    const std::size_t bodyStart =
        headerEnd + headerDelimiter.size();

    const std::size_t responseLength =
        bodyStart + contentLength;

    while (receiveBuffer->size() <
           responseLength)
    {
        const ssize_t received =
            recv(
                socketFd,
                buffer,
                sizeof(buffer),
                0);

        if (received <= 0)
        {
            return false;
        }

        receiveBuffer->append(
            buffer,
            static_cast<std::size_t>(received));
    }

    *response =
        receiveBuffer->substr(
            0,
            responseLength);

    // 只消费当前 HTTP Response，
    // 后面的响应数据继续保留在 receiveBuffer 中。
    receiveBuffer->erase(
        0,
        responseLength);

    return true;
}

std::string MakeGetRequest(
    const std::string &path,
    const std::string &connection = "keep-alive")
{
    return "GET " + path +
           " HTTP/1.1\r\n"
           "Host: 127.0.0.1\r\n"
           "Connection: " +
           connection +
           "\r\n"
           "\r\n";
}

std::string MakePostRequest(
    const std::string &path,
    const std::string &body,
    const std::string &connection = "keep-alive")
{
    return "POST " + path +
           " HTTP/1.1\r\n"
           "Host: 127.0.0.1\r\n"
           "Content-Type: application/json\r\n"
           "Content-Length: " +
           std::to_string(body.size()) +
           "\r\n"
           "Connection: " +
           connection +
           "\r\n"
           "\r\n" +
           body;
}

void StartServer(
    ServerState *state,
    std::thread *serverThread)
{
    assert(state != nullptr);
    assert(serverThread != nullptr);

    *serverThread = std::thread(
        [state]()
        {
            try
            {
                EventLoop loop;

                HttpServer server(
                    &loop,
                    "127.0.0.1",
                    kTestPort);

                server.Router().RegisterRoute(
                    HttpMethod::kGet,
                    "/health",
                    [](const HttpRequest &,
                       HttpResponse &response)
                    {
                        response.SetStatusCode(200);
                        response.SetStatusMessage("OK");
                        response.SetBody(
                            "{\"status\":\"ok\"}");
                    });

                server.Router().RegisterRoute(
                    HttpMethod::kPost,
                    "/hello",
                    [](const HttpRequest &request,
                       HttpResponse &response)
                    {
                        assert(
                            request.Body() ==
                            "{\"a\":1}");

                        response.SetStatusCode(200);
                        response.SetStatusMessage("OK");
                        response.SetBody(
                            "{\"message\":\"hello\"}");
                    });

                server.Router().RegisterRoute(
                    HttpMethod::kGet,
                    "/api/v1/jobs/{id}",
                    [](const HttpRequest &,
                       HttpResponse &response)
                    {
                        response.SetStatusCode(200);
                        response.SetStatusMessage("OK");
                        response.SetBody(
                            "{\"job\":\"ok\"}");
                    });

                server.Start();

                {
                    std::lock_guard<std::mutex> lock(
                        state->mutex);

                    state->loop = &loop;
                    state->ready = true;
                }

                state->condition.notify_one();

                loop.Loop();

                server.Stop();
            }
            catch (...)
            {
                {
                    std::lock_guard<std::mutex> lock(
                        state->mutex);

                    state->exception =
                        std::current_exception();
                    state->ready = true;
                }

                state->condition.notify_one();
            }
        });

    std::unique_lock<std::mutex> lock(
        state->mutex);

    state->condition.wait(
        lock,
        [state]()
        {
            return state->ready;
        });

    if (state->exception)
    {
        std::rethrow_exception(
            state->exception);
    }

    assert(state->loop != nullptr);
}

void StopServer(
    ServerState *state,
    std::thread *serverThread)
{
    assert(state != nullptr);
    assert(serverThread != nullptr);

    EventLoop *loop = nullptr;

    {
        std::lock_guard<std::mutex> lock(
            state->mutex);

        loop = state->loop;
    }

    assert(loop != nullptr);

    loop->QueueInLoop(
        [loop]()
        {
            loop->Quit();
        });

    if (serverThread->joinable())
    {
        serverThread->join();
    }
}

void TestHealth()
{
    const int socketFd =
        CreateClientSocket();

    assert(socketFd >= 0);
    assert(ConnectToServer(socketFd));

    assert(
        SendAll(
            socketFd,
            MakeGetRequest("/health")));

    std::string receiveBuffer;
    std::string response;

    assert(
        ReceiveHttpResponse(
            socketFd,
            &receiveBuffer,
            &response));

    assert(
        response.find(
            "HTTP/1.1 200 OK") !=
        std::string::npos);

    assert(
        response.find(
            "{\"status\":\"ok\"}") !=
        std::string::npos);

    close(socketFd);
}

void TestPostHello()
{
    const int socketFd =
        CreateClientSocket();

    assert(socketFd >= 0);
    assert(ConnectToServer(socketFd));

    const std::string body =
        "{\"a\":1}";

    assert(
        SendAll(
            socketFd,
            MakePostRequest(
                "/hello",
                body)));

    std::string receiveBuffer;
    std::string response;

    assert(
        ReceiveHttpResponse(
            socketFd,
            &receiveBuffer,
            &response));

    assert(
        response.find(
            "HTTP/1.1 200 OK") !=
        std::string::npos);

    assert(
        response.find(
            "{\"message\":\"hello\"}") !=
        std::string::npos);

    close(socketFd);
}

void TestNotFound()
{
    const int socketFd =
        CreateClientSocket();

    assert(socketFd >= 0);
    assert(ConnectToServer(socketFd));

    assert(
        SendAll(
            socketFd,
            MakeGetRequest(
                "/not-found")));

    std::string receiveBuffer;
    std::string response;

    assert(
        ReceiveHttpResponse(
            socketFd,
            &receiveBuffer,
            &response));

    assert(
        response.find(
            "HTTP/1.1 404 Not Found") !=
        std::string::npos);

    assert(
        response.find(
            "404 Not Found") !=
        std::string::npos);

    close(socketFd);
}

void TestInvalidRequest()
{
    const int socketFd =
        CreateClientSocket();

    assert(socketFd >= 0);
    assert(ConnectToServer(socketFd));

    const std::string request =
        "PATCH /hello HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "\r\n";

    assert(
        SendAll(
            socketFd,
            request));

    std::string receiveBuffer;
    std::string response;

    assert(
        ReceiveHttpResponse(
            socketFd,
            &receiveBuffer,
            &response));

    assert(
        response.find(
            "HTTP/1.1 400 Bad Request") !=
        std::string::npos);

    close(socketFd);
}

void TestOversizedBodyIsRejected()
{
    const int socketFd =
        CreateClientSocket();

    assert(socketFd >= 0);
    assert(ConnectToServer(socketFd));

    const std::string request =
        "POST /hello HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Length: 12582913\r\n"
        "\r\n";

    assert(
        SendAll(
            socketFd,
            request));

    std::string receiveBuffer;
    std::string response;

    assert(
        ReceiveHttpResponse(
            socketFd,
            &receiveBuffer,
            &response));

    assert(
        response.find(
            "HTTP/1.1 400 Bad Request") !=
        std::string::npos);

    close(socketFd);
}

void TestKeepAlive()
{
    const int socketFd =
        CreateClientSocket();

    assert(socketFd >= 0);
    assert(ConnectToServer(socketFd));

    std::string receiveBuffer;
    std::string response1;
    std::string response2;

    assert(
        SendAll(
            socketFd,
            MakeGetRequest("/health")));

    assert(
        ReceiveHttpResponse(
            socketFd,
            &receiveBuffer,
            &response1));

    assert(
        response1.find(
            "HTTP/1.1 200 OK") !=
        std::string::npos);

    assert(
        SendAll(
            socketFd,
            MakeGetRequest(
                "/api/v1/jobs/123")));

    assert(
        ReceiveHttpResponse(
            socketFd,
            &receiveBuffer,
            &response2));

    assert(
        response2.find(
            "HTTP/1.1 200 OK") !=
        std::string::npos);

    assert(
        response2.find(
            "{\"job\":\"ok\"}") !=
        std::string::npos);

    close(socketFd);
}

void TestPipelinedRequests()
{
    const int socketFd =
        CreateClientSocket();

    assert(socketFd >= 0);
    assert(ConnectToServer(socketFd));

    const std::string request1 =
        MakeGetRequest("/health");

    const std::string request2 =
        MakeGetRequest(
            "/api/v1/jobs/123");

    assert(
        SendAll(
            socketFd,
            request1 + request2));

    std::string receiveBuffer;
    std::string response1;
    std::string response2;

    assert(
        ReceiveHttpResponse(
            socketFd,
            &receiveBuffer,
            &response1));

    assert(
        ReceiveHttpResponse(
            socketFd,
            &receiveBuffer,
            &response2));

    assert(
        response1.find(
            "HTTP/1.1 200 OK") !=
        std::string::npos);

    assert(
        response1.find(
            "{\"status\":\"ok\"}") !=
        std::string::npos);

    assert(
        response2.find(
            "HTTP/1.1 200 OK") !=
        std::string::npos);

    assert(
        response2.find(
            "{\"job\":\"ok\"}") !=
        std::string::npos);

    close(socketFd);
}

void TestHalfPacket()
{
    const int socketFd =
        CreateClientSocket();

    assert(socketFd >= 0);
    assert(ConnectToServer(socketFd));

    const std::string request =
        MakeGetRequest("/health");

    const std::size_t split =
        request.size() / 2;

    assert(
        SendAll(
            socketFd,
            request.substr(
                0,
                split)));

    std::this_thread::sleep_for(
        std::chrono::milliseconds(20));

    assert(
        SendAll(
            socketFd,
            request.substr(split)));

    std::string receiveBuffer;
    std::string response;

    assert(
        ReceiveHttpResponse(
            socketFd,
            &receiveBuffer,
            &response));

    assert(
        response.find(
            "HTTP/1.1 200 OK") !=
        std::string::npos);

    close(socketFd);
}

void TestConnectionClose()
{
    const int socketFd =
        CreateClientSocket();

    assert(socketFd >= 0);
    assert(ConnectToServer(socketFd));

    assert(
        SendAll(
            socketFd,
            MakeGetRequest(
                "/health",
                "close")));

    std::string receiveBuffer;
    std::string response;

    assert(
        ReceiveHttpResponse(
            socketFd,
            &receiveBuffer,
            &response));

    assert(
        response.find(
            "HTTP/1.1 200 OK") !=
        std::string::npos);

    assert(
        response.find(
            "Connection: close") !=
        std::string::npos);

    char buffer[16];

    const ssize_t result =
        recv(
            socketFd,
            buffer,
            sizeof(buffer),
            0);

    assert(result == 0);

    close(socketFd);
}

}  // namespace

}  // namespace jobpilot::core

int main()
{
    using namespace jobpilot::core;

    ServerState state;
    std::thread serverThread;

    StartServer(
        &state,
        &serverThread);

    TestHealth();
    TestPostHello();
    TestNotFound();
    TestInvalidRequest();
    TestOversizedBodyIsRejected();
    TestKeepAlive();
    TestPipelinedRequests();
    TestHalfPacket();
    TestConnectionClose();

    StopServer(
        &state,
        &serverThread);

    std::cout << "http server tests passed\n";

    return 0;
}
