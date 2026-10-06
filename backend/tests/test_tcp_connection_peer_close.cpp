#include "jobpilot/network/event_loop.h"
#include "jobpilot/network/tcp_connection.h"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

#include <sys/socket.h>
#include <unistd.h>

namespace
{
    struct State
    {
        std::mutex mutex;
        std::condition_variable condition;
        jobpilot::core::EventLoop *loop = nullptr;
        bool connectionClosed = false;
    };
} // namespace

int main()
{
    int sockets[2] = {-1, -1};

    if (::socketpair(
            AF_UNIX,
            SOCK_STREAM,
            0,
            sockets) != 0)
    {
        return 1;
    }

    ::close(sockets[1]);
    sockets[1] = -1;

    State state;

    std::thread loopThread(
        [&state, &sockets]()
        {
            using namespace jobpilot::core;

            EventLoop loop;

            {
                std::lock_guard<std::mutex> lock(
                    state.mutex);

                state.loop = &loop;
            }

            auto connection =
                std::make_shared<TcpConnection>(
                    &loop,
                    "peer-close-send-test",
                    sockets[0]);

            connection->SetConnectionCallback(
                [](const TcpConnection::Ptr &conn)
                {
                    conn->Send("write after peer close");
                });

            connection->SetCloseCallback(
                [&state, &loop](
                    const TcpConnection::Ptr &)
                {
                    {
                        std::lock_guard<std::mutex> lock(
                            state.mutex);

                        state.connectionClosed = true;
                        state.condition.notify_all();
                    }

                    loop.Quit();
                });

            connection->EstablishConnection();

            loop.Loop();

            connection->ConnectDestroyed();
        });

    {
        std::unique_lock<std::mutex> lock(state.mutex);

        const bool closed = state.condition.wait_for(
            lock,
            std::chrono::seconds(2),
            [&state]()
            {
                return state.connectionClosed;
            });

        if (!closed)
        {
            if (state.loop != nullptr)
            {
                state.loop->Quit();
            }

            lock.unlock();
            loopThread.join();

            std::cerr
                << "peer-close send did not close the connection\n";

            return 1;
        }
    }

    loopThread.join();

    std::cout
        << "tcp connection peer-close send regression test passed\n";

    return 0;
}
