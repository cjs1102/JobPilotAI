#pragma once

#include <vector>

namespace jobpilot::core
{
    class Channel; // 前置声明，减少头文件依赖

    class Poller
    {
    public:
        using ChannelList = std::vector<Channel *>;

        Poller() = default;

        virtual ~Poller() = default;

        Poller(const Poller &) = delete;

        Poller &operator=(const Poller &) = delete;

        virtual void Poll(
            int timeoutMs,
            ChannelList *activeChannels) = 0;

        virtual void UpdateChannel(
            Channel *channel) = 0;

        virtual void RemoveChannel(
            Channel *channel) = 0;
    };
}