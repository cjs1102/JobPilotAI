#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <unistd.h>

#include "jobpilot/network/event_loop.h"
#include "jobpilot/network/tcp_connection.h"

using namespace jobpilot::core;

int main()
{
    int sockets[2] = {-1, -1};

    const int result = ::socketpair(
        AF_UNIX,
        SOCK_STREAM,
        0,
        sockets);

    assert(result == 0);

    const std::string message = "hello from TcpConnection";

    std::thread loopThread(
        [&]()
        {
            EventLoop loop;

            auto connection =
                std::make_shared<TcpConnection>(
                    &loop,
                    "send-test",
                    sockets[0]);

            connection->SetConnectionCallback(
                [&](const TcpConnection::Ptr &conn)
                {
                    assert(conn->IsConnected());

                    // 当前处于 EventLoop 所在线程，
                    // Send() 会直接调用 SendInLoop()。
                    conn->Send(message);

                    // 发送完成后退出事件循环。
                    loop.Quit();
                });

            connection->EstablishConnection();

            loop.Loop();

            connection->ConnectDestroyed();
        });

    loopThread.join();

    char buffer[128] = {};

    const ssize_t receivedBytes = ::read(
        sockets[1],
        buffer,
        sizeof(buffer));

    assert(receivedBytes ==
           static_cast<ssize_t>(message.size()));

    const std::string receivedMessage(
        buffer,
        static_cast<std::size_t>(receivedBytes));

    assert(receivedMessage == message);

    ::close(sockets[1]);

    std::cout << "test_tcp_connection_send passed"
              << std::endl;

    return 0;
}
