#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "jobpilot/network/tcp_server.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "jobpilot/network/channel.h"
#include "jobpilot/network/event_loop.h"

namespace jobpilot::core
{

    TcpServer::TcpServer(EventLoop *loop,
                         const std::string &ip,
                         uint16_t port)
        : loop_(loop),
          ip_(ip),
          port_(port),
          listenFd_(-1),
          acceptChannel_(nullptr),
          nextConnectionId_(1),
          started_(false)
    {
    }

    TcpServer::~TcpServer()
    {
        Stop();
    }

    void TcpServer::Start()
    {
        if (started_)
        {
            return;
        }

        if (loop_ == nullptr)
        {
            throw std::invalid_argument("TcpServer requires a valid EventLoop");
        }

        listenFd_ = ::socket(
            AF_INET,
            SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
            0);

        if (listenFd_ < 0)
        {
            throw std::runtime_error(
                "socket failed: " + std::string(std::strerror(errno)));
        }

        int reuseAddress = 1;

        if (::setsockopt(
                listenFd_,
                SOL_SOCKET,
                SO_REUSEADDR,
                &reuseAddress,
                sizeof(reuseAddress)) < 0)
        {
            ::close(listenFd_);
            listenFd_ = -1;

            throw std::runtime_error(
                "setsockopt failed: " + std::string(std::strerror(errno)));
        }

        sockaddr_in serverAddress{};
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_port = htons(port_);

        if (ip_.empty())
        {
            serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
        }
        else
        {
            if (::inet_pton(
                    AF_INET,
                    ip_.c_str(),
                    &serverAddress.sin_addr) <= 0)
            {
                ::close(listenFd_);
                listenFd_ = -1;

                throw std::invalid_argument("invalid IPv4 address: " + ip_);
            }
        }

        if (::bind(
                listenFd_,
                reinterpret_cast<sockaddr *>(&serverAddress),
                sizeof(serverAddress)) < 0)
        {
            const std::string errorMessage =
                "bind failed: " + std::string(std::strerror(errno));

            ::close(listenFd_);
            listenFd_ = -1;

            throw std::runtime_error(errorMessage);
        }

        if (::listen(listenFd_, SOMAXCONN) < 0)
        {
            const std::string errorMessage =
                "listen failed: " + std::string(std::strerror(errno));

            ::close(listenFd_);
            listenFd_ = -1;

            throw std::runtime_error(errorMessage);
        }

        acceptChannel_ = std::make_unique<Channel>(listenFd_);

        acceptChannel_->SetReadCallback(
            [this]()
            {
                HandleAccept();
            });

        acceptChannel_->SetEvents(EPOLLIN);

        loop_->UpdateChannel(acceptChannel_.get());

        started_ = true;
    }

    void TcpServer::Stop()
    {
        if (!started_)
        {
            return;
        }

        if (loop_ != nullptr && acceptChannel_ != nullptr)
        {
            loop_->RemoveChannel(acceptChannel_.get());
            acceptChannel_.reset();
        }

        for (auto &item : connections_)
        {
            item.second->ConnectDestroyed();
        }

        connections_.clear();

        if (listenFd_ >= 0)
        {
            ::close(listenFd_);
            listenFd_ = -1;
        }

        started_ = false;
    }

    void TcpServer::SetConnectionCallback(
        const ConnectionCallback &callback)
    {
        connectionCallback_ = callback;
    }

    void TcpServer::SetMessageCallback(
        const MessageCallback &callback)
    {
        messageCallback_ = callback;
    }

    void TcpServer::SetWriteCompleteCallback(
        const WriteCompleteCallback &callback)
    {
        writeCompleteCallback_ = callback;
    }

    void TcpServer::SetCloseCallback(
        const TcpConnection::CloseCallback &callback)
    {
        closeCallback_ = callback;
    }

    void TcpServer::HandleAccept()
    {
        while (true)
        {
            sockaddr_in clientAddress{};
            socklen_t clientAddressLength = sizeof(clientAddress);

            const int clientFd = ::accept4(
                listenFd_,
                reinterpret_cast<sockaddr *>(&clientAddress),
                &clientAddressLength,
                SOCK_NONBLOCK | SOCK_CLOEXEC);

            if (clientFd >= 0)
            {
                const std::string connectionName =
                    "connection-" + std::to_string(nextConnectionId_++);

                auto connection = std::make_shared<TcpConnection>(
                    loop_,
                    connectionName,
                    clientFd);

                connection->SetConnectionCallback(connectionCallback_);
                connection->SetMessageCallback(messageCallback_);
                connection->SetWriteCompleteCallback(writeCompleteCallback_);

                connection->SetCloseCallback(
                    [this](const TcpConnection::Ptr &closedConnection)
                    {
                        if (closeCallback_)
                        {
                            closeCallback_(closedConnection);
                        }

                        loop_->QueueInLoop(
                            [this, closedConnection]()
                            {
                                RemoveConnection(closedConnection);
                            });
                    });

                connections_.emplace(connectionName, connection);

                connection->ConnectEstablished();

                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }

            if (errno == EINTR)
            {
                continue;
            }

            break;
        }
    }

    void TcpServer::RemoveConnection(
        const TcpConnection::Ptr &connection)
    {
        if (connection == nullptr)
        {
            return;
        }

        connections_.erase(connection->Name());
        connection->ConnectDestroyed();
    }

} // namespace jobpilot::core