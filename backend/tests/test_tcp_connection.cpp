
#include <cassert>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <chrono>

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

    std::mutex mutex;
    std::condition_variable condition;

    bool connectionReady = false;
    bool messageReceived = false;
    std::string receivedMessage;

    std::thread loopThread(
        [&]()
        {
            EventLoop loop;

            auto connection =
                std::make_shared<TcpConnection>(
                    &loop,
                    "receive-test",
                    sockets[0]);

            connection->SetConnectionCallback(
                [&](const TcpConnection::Ptr &conn)
                {
                    assert(conn->IsConnected());

                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        connectionReady = true;
                    }

                    condition.notify_one();
                });

            connection->SetMessageCallback(
                [&](const TcpConnection::Ptr &conn,
                    Buffer *buffer)
                {
                    assert(conn->IsConnected());

                    receivedMessage =
                        buffer->RetrieveAsString();

                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        messageReceived = true;
                    }

                    condition.notify_one();

                    loop.Quit();
                });

            connection->EstablishConnection();

            {
                std::lock_guard<std::mutex> lock(mutex);
                connectionReady = true;
            }

            condition.notify_one();

            loop.Loop();

            connection->ConnectDestroyed();
        });

    {
        std::unique_lock<std::mutex> lock(mutex);

        const bool ready = condition.wait_for(
            lock,
            std::chrono::seconds(2),
            [&]()
            {
                return connectionReady;
            });

        assert(ready);
    }

    const std::string message = "hello TcpConnection";

    const ssize_t written = ::write(
        sockets[1],
        message.data(),
        message.size());

    assert(written == static_cast<ssize_t>(message.size()));

    {
        std::unique_lock<std::mutex> lock(mutex);

        const bool received = condition.wait_for(
            lock,
            std::chrono::seconds(2),
            [&]()
            {
                return messageReceived;
            });

        assert(received);
    }

    loopThread.join();

    assert(receivedMessage == message);

    ::close(sockets[1]);

    std::cout << "test_tcp_connection passed" << std::endl;

    return 0;
}
