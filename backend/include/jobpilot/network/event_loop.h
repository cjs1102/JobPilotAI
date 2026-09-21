#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "jobpilot/network/poller.h"

namespace jobpilot::core
{

    class Channel;

    class EventLoop
    {
    public:
        using Functor = std::function<void()>;

        EventLoop();
        ~EventLoop();

        EventLoop(const EventLoop &) = delete;
        EventLoop &operator=(const EventLoop &) = delete;

        void Loop();

        void Quit();

        void RunInLoop(Functor cb);

        void QueueInLoop(Functor cb);

        void UpdateChannel(Channel *channel);

        void RemoveChannel(Channel *channel);
        bool IsInLoopThread() const;

    private:
        void Wakeup();

        void HandleRead();

        void DoPendingFunctors();

    private:
        bool looping_;

        std::atomic<bool> quit_;

        std::thread::id threadId_;

        std::unique_ptr<Poller> poller_;

        Poller::ChannelList activeChannels_;

        int wakeupFd_;

        std::unique_ptr<Channel> wakeupChannel_;

        std::mutex mutex_;

        std::vector<Functor> pendingFunctors_;
    };

} // namespace jobpilot::core