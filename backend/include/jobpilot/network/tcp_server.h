#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "jobpilot/network/tcp_connection.h"

namespace jobpilot::core
{
    class Channel;
    class EventLoop;

    class TcpServer
    {
    public:
        using ConnectionCallback = TcpConnection::ConnectionCallback;
        using MessageCallback = TcpConnection::MessageCallback;
        using WriteCompleteCallback = TcpConnection::WriteCompleteCallback;

        TcpServer(EventLoop *loop,
                  const std::string &ip,
                  uint16_t port);
        ~TcpServer();

        TcpServer(const TcpServer &) = delete;
        TcpServer &operator=(const TcpServer &) = delete;

        void Start();
        void Stop();

        void SetConnectionCallback(
            const ConnectionCallback &callback);

        void SetMessageCallback(
            const MessageCallback &callback);

        void SetWriteCompleteCallback(
            const WriteCompleteCallback &callback);

        void SetCloseCallback(
            const TcpConnection::CloseCallback &callback);

    private:
        void HandleAccept();
        void RemoveConnection(
            const TcpConnection::Ptr &connection);

        EventLoop *loop_;

        std::string ip_;
        uint16_t port_;

        int listenFd_;

        std::unique_ptr<Channel> acceptChannel_;

        std::unordered_map<
            std::string,
            TcpConnection::Ptr>
            connections_;

        ConnectionCallback connectionCallback_;
        MessageCallback messageCallback_;
        WriteCompleteCallback writeCompleteCallback_;
        TcpConnection::CloseCallback closeCallback_;

        uint64_t nextConnectionId_;

        bool started_;
    };
} // namespace jobpilot::core