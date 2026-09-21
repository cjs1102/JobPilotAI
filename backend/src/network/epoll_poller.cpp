#include "jobpilot/network/epoll_poller.h"

#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

#include "jobpilot/network/channel.h"

namespace jobpilot::core
{
    namespace
    {
        constexpr int kInitialEventListSize = 16;
    }

    EpollPoller::EpollPoller()
        : epollFd_(::epoll_create1(EPOLL_CLOEXEC)),
          events_(kInitialEventListSize)
    {
        if (epollFd_ < 0)
        {
            throw std::runtime_error(
                std::string("epoll_create1 failed: ") +
                std::strerror(errno));
        }
    }

    EpollPoller::~EpollPoller()
    {
        if (epollFd_ >= 0)
        {
            ::close(epollFd_);
        }
    }

    void EpollPoller::Poll(
        int timeoutMs,
        ChannelList *activeChannels)
    {
        // numEvents：
        // epoll_wait() 实际返回的事件数量
        int numEvents = ::epoll_wait(
            epollFd_,
            events_.data(),
            static_cast<int>(events_.size()),
            timeoutMs);

        if (numEvents > 0)
        {
            FillActiveChannels(
                numEvents,
                activeChannels);

            // 如果本次事件数量刚好把 events_ 填满，
            // 说明可能还有更多事件，下次扩大容量。
            if (numEvents == static_cast<int>(events_.size()))
            {
                events_.resize(events_.size() * 2);
            }
        }
        else if (numEvents < 0 && errno != EINTR)
        {
            throw std::runtime_error(
                std::string("epoll_wait failed: ") +
                std::strerror(errno));
        }
    }

    void EpollPoller::UpdateChannel(Channel *channel)
    {
        const int fd = channel->Fd();

        if (registeredFds_.find(fd) == registeredFds_.end())
        {
            Update(EPOLL_CTL_ADD, channel);
            registeredFds_.insert(fd);
        }
        else
        {
            Update(EPOLL_CTL_MOD, channel);
        }
    }

    void EpollPoller::RemoveChannel(Channel *channel)
    {
        const int fd = channel->Fd();

        if (registeredFds_.find(fd) != registeredFds_.end())
        {
            Update(EPOLL_CTL_DEL, channel);
            registeredFds_.erase(fd);
        }
    }

    void EpollPoller::FillActiveChannels(
        int numEvents,
        ChannelList *activeChannels) const
    {
        for (int i = 0; i < numEvents; ++i)
        {
            Channel *channel =
                static_cast<Channel *>(
                    events_[i].data.ptr);

            channel->SetRevents(
                events_[i].events);

            activeChannels->push_back(channel);
        }
    }

    void EpollPoller::Update(
        int operation,
        Channel *channel)
    {
        struct epoll_event event{};

        event.events = channel->Events();

        event.data.ptr = channel;

        if (::epoll_ctl(
                epollFd_,
                operation,
                channel->Fd(),
                &event) < 0)
        {
            throw std::runtime_error(
                std::string("epoll_ctl failed: ") +
                std::strerror(errno));
        }
    }
}