#include <arpa/inet.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include "jobpilot/network/event_loop.h"
#include "jobpilot/network/tcp_server.h"

using namespace jobpilot::core;

int main()
{
    constexpr uint16_t kTestPort = 19090;

    EventLoop loop;
    TcpServer server(&loop, "127.0.0.1", kTestPort);

    std::atomic<bool> connectionEstablished{false};
    std::atomic<bool> messageReceived{false};
    std::atomic<bool> connectionClosed{false};

    server.SetConnectionCallback(
        [&connectionEstablished](const TcpConnection::Ptr &)
        {
            connectionEstablished.store(true);
        });

    server.SetMessageCallback(
        [](const TcpConnection::Ptr &connection, Buffer *buffer)
        {
            const std::string message = buffer->RetrieveAsString();

            if (message == "hello")
            {
                connection->Send("echo: hello");
            }
        });

    server.SetConnectionCallback(
        [&connectionEstablished](const TcpConnection::Ptr &)
        {
            connectionEstablished.store(true);
        });
    server.SetCloseCallback(
        [&connectionClosed](const TcpConnection::Ptr &)
        {
            connectionClosed.store(true);
        });
    server.Start();

    std::thread loopThread(
        [&loop]()
        {
            loop.Loop();
        });

    std::this_thread::sleep_for(
        std::chrono::milliseconds(100));

    const int clientFd = ::socket(
        AF_INET,
        SOCK_STREAM,
        0);

    assert(clientFd >= 0);

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(kTestPort);

    const int addressResult = ::inet_pton(
        AF_INET,
        "127.0.0.1",
        &serverAddress.sin_addr);

    assert(addressResult == 1);

    const int connectResult = ::connect(
        clientFd,
        reinterpret_cast<sockaddr *>(&serverAddress),
        sizeof(serverAddress));

    assert(connectResult == 0);

    const std::string request = "hello";

    const ssize_t sendResult = ::send(
        clientFd,
        request.data(),
        request.size(),
        0);

    assert(sendResult == static_cast<ssize_t>(request.size()));

    char response[128]{};

    const ssize_t receiveResult = ::recv(
        clientFd,
        response,
        sizeof(response) - 1,
        0);

    assert(receiveResult > 0);

    response[receiveResult] = '\0';

    assert(std::string(response) == "echo: hello");

    assert(connectionEstablished.load());

    ::close(clientFd);

    // 等待服务端检测到客户端断开
    for (int i = 0; i < 20 && !connectionClosed.load(); ++i)
    {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(50));
    }

    assert(connectionClosed.load());

    loop.QueueInLoop(
        [&server, &loop]()
        {
            server.Stop();
            loop.Quit();
        });

    loopThread.join();

    std::cout << "tcp server echo test passed" << std::endl;

    return 0;
}