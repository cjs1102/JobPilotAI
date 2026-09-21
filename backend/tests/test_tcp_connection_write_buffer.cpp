#include <cassert>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <fcntl.h>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <algorithm>
#include <sys/socket.h>
#include <unistd.h>

#include "jobpilot/network/event_loop.h"
#include "jobpilot/network/tcp_connection.h"

using namespace jobpilot::core;

int main()
{
    int sockets[2] = {-1, -1};

    assert(::socketpair(
        AF_UNIX,
        SOCK_STREAM,
        0,
        sockets) == 0);

    // 将 TcpConnection 使用的 socket 设置为非阻塞
    const int flags = ::fcntl(
        sockets[0],
        F_GETFL,
        0);

    assert(flags >= 0);

    assert(::fcntl(
        sockets[0],
        F_SETFL,
        flags | O_NONBLOCK) == 0);

    // 尽可能减小发送缓冲区，便于触发 EAGAIN
    int sendBufferSize = 4096;

    assert(::setsockopt(
        sockets[0],
        SOL_SOCKET,
        SO_SNDBUF,
        &sendBufferSize,
        sizeof(sendBufferSize)) == 0);

    // 预先填满发送缓冲区
    const std::string filler(1024, 'F');

    std::size_t fillerBytes = 0;

    while (true)
    {
        const ssize_t written = ::write(
            sockets[0],
            filler.data(),
            filler.size());

        if (written > 0)
        {
            fillerBytes += static_cast<std::size_t>(written);
            continue;
        }

        if (written < 0 && errno == EAGAIN)
        {
            break;
        }

        assert(false);
    }

    assert(fillerBytes > 0);

    const std::string message(
        256 * 1024,
        'M');

    std::mutex mutex;
    std::condition_variable condition;

    bool sendIssued = false;
    bool writeCompleted = false;
    bool loopFinished = false;

    std::thread loopThread([&]() {
        EventLoop loop;

        auto connection = std::make_shared<TcpConnection>(
            &loop,
            "write-buffer-test",
            sockets[0]);

        connection->SetConnectionCallback(
            [&](const TcpConnection::Ptr& conn) {
                assert(conn->IsConnected());

                conn->SetWriteCompleteCallback(
                    [&](const TcpConnection::Ptr&) {
                        {
                            std::lock_guard<std::mutex> lock(mutex);
                            writeCompleted = true;
                        }

                        condition.notify_one();
                        loop.Quit();
                    });

                conn->Send(message);

                {
                    std::lock_guard<std::mutex> lock(mutex);
                    sendIssued = true;
                }

                condition.notify_one();
            });

        connection->EstablishConnection();

        loop.Loop();

        connection->ConnectDestroyed();

        {
            std::lock_guard<std::mutex> lock(mutex);
            loopFinished = true;
        }

        condition.notify_one();
    });

    // 等待 Send() 已经被调用
    {
        std::unique_lock<std::mutex> lock(mutex);

        const bool ready = condition.wait_for(
            lock,
            std::chrono::seconds(2),
            [&]() {
                return sendIssued;
            });

        assert(ready);
    }

    // 现在开始读取对端数据，使发送缓冲区恢复可写
    std::size_t receivedMessageBytes = 0;
    std::string receivedMessage;

    receivedMessage.reserve(message.size());

    char buffer[8192];

    while (receivedMessageBytes < message.size())
    {
        const ssize_t received = ::read(
            sockets[1],
            buffer,
            sizeof(buffer));

        assert(received > 0);

        std::size_t remaining =
            message.size() - receivedMessageBytes;

        std::size_t usableBytes =
            static_cast<std::size_t>(received);

        // 前面填充的数据需要丢弃
        if (fillerBytes > 0)
        {
            const std::size_t discarded =
                std::min(fillerBytes, usableBytes);

            fillerBytes -= discarded;
            usableBytes -= discarded;
        }

        if (usableBytes > 0)
        {
            receivedMessage.append(
                buffer + (received - usableBytes),
                usableBytes);

            receivedMessageBytes += usableBytes;
        }
    }

    {
        std::unique_lock<std::mutex> lock(mutex);

        const bool completed = condition.wait_for(
            lock,
            std::chrono::seconds(2),
            [&]() {
                return writeCompleted;
            });

        assert(completed);
    }

    loopThread.join();

    assert(receivedMessage == message);

    ::close(sockets[1]);

    std::cout
        << "test_tcp_connection_write_buffer passed"
        << std::endl;

    return 0;
}
