#pragma once

#include <cstdint>
#include <functional>

namespace jobpilot::core
{
    class Channel
    {
    public:
        using EventCallback = std::function<void()>;

        explicit Channel(int fd);

        ~Channel();

        Channel(const Channel &) = delete;
        Channel &operator=(const Channel &) = delete;

        int Fd() const;

        uint32_t Events() const;

        uint32_t Revents() const;

        void SetEvents(uint32_t events);

        void SetRevents(uint32_t revents);

        void SetReadCallback(const EventCallback &cb);

        void SetWriteCallback(const EventCallback &cb);

        void SetCloseCallback(const EventCallback &cb);

        void HandleEvent();

    private:
        int fd_;

        // 当前 Channel 关注的事件
        uint32_t events_;

        // epoll_wait 实际返回的事件
        uint32_t revents_;

        EventCallback readCallback_;

        EventCallback writeCallback_;

        EventCallback closeCallback_;
    };
}