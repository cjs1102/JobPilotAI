#include "jobpilot/network/channel.h"

#include <sys/epoll.h>

namespace jobpilot::core
{
    Channel::Channel(int fd)
        : fd_(fd),
          events_(0),
          revents_(0)
    {
    }

    Channel::~Channel() = default;

    int Channel::Fd() const
    {
        return fd_;
    }

    uint32_t Channel::Events() const
    {
        return events_;
    }

    uint32_t Channel::Revents() const
    {
        return revents_;
    }

    void Channel::SetEvents(uint32_t events)
    {
        events_ = events;
    }

    void Channel::SetRevents(uint32_t revents)
    {
        revents_ = revents;
    }

    void Channel::SetReadCallback(const EventCallback &cb)
    {
        readCallback_ = cb;
    }

    void Channel::SetWriteCallback(const EventCallback &cb)
    {
        writeCallback_ = cb;
    }

    void Channel::SetCloseCallback(const EventCallback &cb)
    {
        closeCallback_ = cb;
    }

    void Channel::HandleEvent()
    {
        const bool hasErrorOrHangup =
            (revents_ & (EPOLLERR | EPOLLHUP)) != 0;

        // 先尝试读出已经到达的数据，即使同一个事件同时带
        // EPOLLERR/EPOLLHUP，也不能在读取前关闭连接。
        if (hasErrorOrHangup ||
            (revents_ & EPOLLIN) != 0)
        {
            if (readCallback_)
            {
                readCallback_();
            }
        }

        if (hasErrorOrHangup)
        {
            if (closeCallback_)
            {
                closeCallback_();
            }

            return;
        }

        // 可写
        if ((revents_ & EPOLLOUT) != 0)
        {
            if (writeCallback_)
            {
                writeCallback_();
            }
        }
    }
}
