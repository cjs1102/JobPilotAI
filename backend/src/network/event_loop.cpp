#include "jobpilot/network/event_loop.h"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

#include "jobpilot/network/channel.h"
#include "jobpilot/network/epoll_poller.h"

namespace jobpilot::core
{

    namespace
    {

        int CreateEventFd()
        {
            int eventFd = ::eventfd(
                0,
                EFD_NONBLOCK | EFD_CLOEXEC);

            if (eventFd < 0)
            {
                throw std::runtime_error(
                    std::string("eventfd failed: ") +
                    std::strerror(errno));
            }

            return eventFd;
        }

    } // namespace

    EventLoop::EventLoop()
        : looping_(false),
          quit_(false),
          threadId_(std::this_thread::get_id()),
          poller_(std::make_unique<EpollPoller>()),
          wakeupFd_(CreateEventFd()),
          wakeupChannel_(std::make_unique<Channel>(wakeupFd_))
    {
        // EventLoop 需要监听 eventfd 的可读事件。
        wakeupChannel_->SetEvents(EPOLLIN);

        // 当 eventfd 被其他线程写入后，
        // epoll_wait 返回，Channel 会调用这个回调。
        wakeupChannel_->SetReadCallback(
            [this]()
            {
                HandleRead();
            });

        // 将 wakeupChannel 注册到 epoll。
        UpdateChannel(wakeupChannel_.get());
    }

    EventLoop::~EventLoop()
    {
        if (wakeupFd_ >= 0)
        {
            ::close(wakeupFd_);
        }
    }

    bool EventLoop::IsInLoopThread() const
    {
        return threadId_ == std::this_thread::get_id();
    }

    void EventLoop::Loop()
    {
        if (looping_)
        {
            throw std::runtime_error(
                "EventLoop is already looping");
        }

        looping_ = true;

        while (!quit_.load())
        {
            // 清空上一轮已经处理过的 Channel。
            activeChannels_.clear();

            // 阻塞等待 epoll 事件。
            poller_->Poll(
                -1,
                &activeChannels_);

            // 依次处理本轮发生事件的 Channel。
            for (Channel *channel : activeChannels_)
            {
                channel->HandleEvent();
            }

            // 执行其他线程提交过来的任务。
            DoPendingFunctors();
        }

        looping_ = false;
    }

    void EventLoop::Quit()
    {
        quit_.store(true);

        // 如果 Loop 当前阻塞在 epoll_wait，
        // 通过 eventfd 将它唤醒。
        Wakeup();
    }

    void EventLoop::RunInLoop(Functor cb)
    {
        if (IsInLoopThread())
        {
            // 当前线程就是 EventLoop 所在线程，
            // 可以直接执行。
            cb();
        }
        else
        {
            // 其他线程不能直接操作 EventLoop，
            // 放入任务队列，然后唤醒 EventLoop。
            QueueInLoop(std::move(cb));
        }
    }

    void EventLoop::QueueInLoop(Functor cb)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);

            pendingFunctors_.push_back(std::move(cb));
        }

        // 通知 EventLoop：
        // “有新的任务来了。”
        Wakeup();
    }

    void EventLoop::UpdateChannel(Channel *channel)
    {
        poller_->UpdateChannel(channel);
    }

    void EventLoop::RemoveChannel(Channel *channel)
    {
        poller_->RemoveChannel(channel);
    }

    void EventLoop::Wakeup()
    {
        std::uint64_t one = 1;

        const ssize_t written = ::write(
            wakeupFd_,
            &one,
            sizeof(one));

        if (written < 0 && errno != EAGAIN)
        {
            throw std::runtime_error(
                std::string("eventfd write failed: ") +
                std::strerror(errno));
        }
    }

    void EventLoop::HandleRead()
    {
        std::uint64_t value = 0;

        const ssize_t readBytes = ::read(
            wakeupFd_,
            &value,
            sizeof(value));

        if (readBytes < 0 && errno != EAGAIN)
        {
            throw std::runtime_error(
                std::string("eventfd read failed: ") +
                std::strerror(errno));
        }
    }

    void EventLoop::DoPendingFunctors()
    {
        std::vector<Functor> functors;

        {
            std::lock_guard<std::mutex> lock(mutex_);

            // 将任务从共享队列中一次性拿出来。
            functors.swap(pendingFunctors_);
        }

        // 释放 mutex 后再执行任务。
        //
        // 这样做非常重要：
        // 回调执行过程中如果又调用 QueueInLoop，
        // 就不会因为 mutex 还被当前线程持有而产生死锁。
        for (const Functor &functor : functors)
        {
            functor();
        }
    }

} // namespace jobpilot::core