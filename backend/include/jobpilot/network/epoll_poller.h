#pragma once

#include <sys/epoll.h>

#include <vector>

#include <unordered_set>

#include "jobpilot/network/poller.h"

namespace jobpilot::core
{
    class EpollPoller : public Poller
    {
    public:
        EpollPoller();

        ~EpollPoller();

        EpollPoller(const EpollPoller &) = delete;

        EpollPoller &operator=(const EpollPoller &) = delete;

        void Poll(
            int timeoutMs,
            ChannelList *activeChannels) override;

        void UpdateChannel(
            Channel *channel) override;

        void RemoveChannel(
            Channel *channel) override;

    private:
        void FillActiveChannels(
            int numEvents,
            ChannelList *activeChannels) const;

        void Update(
            int operation,
            Channel *channel);

    private:
        int epollFd_;

        std::unordered_set<int> registeredFds_;

        std::vector<struct epoll_event> events_;
    };
}