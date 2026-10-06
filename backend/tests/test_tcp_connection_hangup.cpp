#include "jobpilot/network/event_loop.h"
#include "jobpilot/network/tcp_connection.h"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
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
        bool ready = false;
        bool messageReceived = false;
        bool connectionClosed = false;
        std::string received;
    };

    void NotifyAll(State *state)
    {
        state->condition.notify_all();
    }
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
                    "hangup-test",
                    sockets[0]);

            connection->SetConnectionCallback(
                [&state](const TcpConnection::Ptr &)
                {
                    std::lock_guard<std::mutex> lock(
                        state.mutex);

                    state.ready = true;
                    NotifyAll(&state);
                });

            connection->SetMessageCallback(
                [&state](
                    const TcpConnection::Ptr &,
                    Buffer *buffer)
                {
                    const std::string received =
                        buffer->RetrieveAsString();

                    std::lock_guard<std::mutex> lock(
                        state.mutex);

                    state.received = received;
                    state.messageReceived = true;
                    NotifyAll(&state);
                });

            connection->SetCloseCallback(
                [&state, &loop](
                    const TcpConnection::Ptr &)
                {
                    {
                        std::lock_guard<std::mutex> lock(
                            state.mutex);

                        state.connectionClosed = true;
                        NotifyAll(&state);
                    }

                    loop.Quit();
                });

            connection->EstablishConnection();

            loop.Loop();

            connection->ConnectDestroyed();
        });

    {
        std::unique_lock<std::mutex> lock(state.mutex);

        const bool ready = state.condition.wait_for(
            lock,
            std::chrono::seconds(2),
            [&state]()
            {
                return state.ready;
            });

        if (!ready)
        {
            lock.unlock();

            {
                std::lock_guard<std::mutex> stateLock(
                    state.mutex);

                if (state.loop != nullptr)
                {
                    state.loop->Quit();
                }
            }

            loopThread.join();
            ::close(sockets[1]);

            std::cerr
                << "connection establishment timed out\n";

            return 1;
        }
    }

    const std::string payload =
        "final bytes before hangup";

    const ssize_t written = ::write(
        sockets[1],
        payload.data(),
        payload.size());

    if (written !=
        static_cast<ssize_t>(payload.size()))
    {
        ::close(sockets[1]);
        loopThread.join();
        return 1;
    }

    ::shutdown(sockets[1], SHUT_WR);
    ::close(sockets[1]);
    sockets[1] = -1;

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
                << "hangup close callback timed out\n";

            return 1;
        }
    }

    loopThread.join();

    if (!state.messageReceived ||
        state.received != payload)
    {
        std::cerr
            << "readable data was discarded before hangup\n";

        return 1;
    }

    std::cout
        << "tcp connection hangup regression test passed\n";

    return 0;
}
