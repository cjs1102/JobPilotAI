#include "jobpilot/network/tcp_connection.h"

#include <utility>
#include <cerrno>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "jobpilot/network/channel.h"
#include "jobpilot/network/event_loop.h"

namespace jobpilot::core
{

    TcpConnection::TcpConnection(
        EventLoop *loop,
        std::string name,
        int socketFd)
        : loop_(loop),
          name_(std::move(name)),
          socketFd_(socketFd),
          channel_(std::make_unique<Channel>(socketFd)),
          state_(State::kConnecting)
    {
    }

    TcpConnection::~TcpConnection()
    {
        if (socketFd_ >= 0)
        {
            ::close(socketFd_);
            socketFd_ = -1;
        }
    }

    EventLoop *TcpConnection::GetLoop() const
    {
        return loop_;
    }

    const std::string &TcpConnection::Name() const
    {
        return name_;
    }

    int TcpConnection::Fd() const
    {
        return socketFd_;
    }

    TcpConnection::State TcpConnection::GetState() const
    {
        return state_;
    }

    bool TcpConnection::IsConnected() const
    {
        return state_ == State::kConnected;
    }

    bool TcpConnection::IsDisconnected() const
    {
        return state_ == State::kDisconnected;
    }

    void TcpConnection::SetState(State state)
    {
        state_ = state;
    }

    void TcpConnection::SetConnectionCallback(
        const ConnectionCallback &callback)
    {
        connectionCallback_ = callback;
    }

    void TcpConnection::SetMessageCallback(
        const MessageCallback &callback)
    {
        messageCallback_ = callback;
    }

    void TcpConnection::SetWriteCompleteCallback(
        const WriteCompleteCallback &callback)
    {
        writeCompleteCallback_ = callback;
    }

    void TcpConnection::SetCloseCallback(
        const CloseCallback &callback)
    {
        closeCallback_ = callback;
    }

    void TcpConnection::EstablishConnection()
    {
        SetState(State::kConnected);

        ConnectEstablished();
    }

    void TcpConnection::HandleRead()
    {
        int savedErrno = 0;

        ssize_t n = inputBuffer_.ReadFd(
            socketFd_,
            &savedErrno);

        if (n > 0)
        {
            if (messageCallback_)
            {
                messageCallback_(
                    shared_from_this(),
                    &inputBuffer_);
            }
        }
        else if (n == 0)
        {
            HandleClose();
        }
        else
        {
            if (savedErrno != EAGAIN &&
                savedErrno != EINTR)
            {
                HandleClose();
            }
        }
    }

    void TcpConnection::HandleWrite()
    {
        if (!channel_)
        {
            return;
        }

        int savedErrno = 0;

        ssize_t n = outputBuffer_.WriteFd(
            socketFd_,
            &savedErrno);

        if (n > 0)
        {
            // 数据已经发送完成
            if (outputBuffer_.ReadableBytes() == 0)
            {
                // 不再监听写事件
                channel_->SetEvents(
                    EPOLLIN);

                loop_->UpdateChannel(
                    channel_.get());

                if (writeCompleteCallback_)
                {
                    writeCompleteCallback_(
                        shared_from_this());
                }
            }
        }
        else
        {
            if (savedErrno != EAGAIN &&
                savedErrno != EINTR)
            {
                HandleClose();
            }
        }
    }

    void TcpConnection::HandleClose()
    {
        if (state_ == State::kDisconnected)
        {
            return;
        }

        SetState(State::kDisconnected);

        channel_->SetEvents(0);

        loop_->RemoveChannel(
            channel_.get());

        if (closeCallback_)
        {
            closeCallback_(
                shared_from_this());
        }
    }

    void TcpConnection::ConnectEstablished()
    {
        SetState(State::kConnected);

        channel_->SetReadCallback(
            [this]()
            {
                HandleRead();
            });

        channel_->SetWriteCallback(
            [this]()
            {
                HandleWrite();
            });

        channel_->SetCloseCallback(
            [this]()
            {
                HandleClose();
            });

        channel_->SetEvents(EPOLLIN);

        loop_->UpdateChannel(channel_.get());

        if (connectionCallback_)
        {
            connectionCallback_(
                shared_from_this());
        }
    }

    void TcpConnection::ConnectDestroyed()
    {
        if (IsConnected())
        {
            SetState(State::kDisconnected);
        }

        loop_->RemoveChannel(channel_.get());
    }

    void TcpConnection::Shutdown()
    {
        if (IsConnected())
        {
            SetState(State::kDisconnecting);

            loop_->RunInLoop(
                [this]()
                {
                    ShutdownInLoop();
                });
        }
    }

    void TcpConnection::ShutdownInLoop()
    {
        if (state_ == State::kDisconnecting)
        {
            ::shutdown(
                socketFd_,
                SHUT_WR);
        }
    }

    void TcpConnection::Send(
        const std::string &message)
    {
        if (IsConnected())
        {
            if (loop_->IsInLoopThread())
            {
                SendInLoop(message);
            }
            else
            {
                loop_->RunInLoop(
                    [this, message]()
                    {
                        SendInLoop(message);
                    });
            }
        }
    }

    void TcpConnection::SendInLoop(
        const std::string &message)
    {
        if (state_ != State::kConnected)
        {
            return;
        }

        ssize_t n = 0;

        if (outputBuffer_.ReadableBytes() == 0)
        {
            n = ::write(
                socketFd_,
                message.data(),
                message.size());

            if (n < 0)
            {
                const int savedErrno = errno;

                if (savedErrno != EAGAIN &&
                    savedErrno != EINTR)
                {
                    HandleClose();
                    return;
                }

                n = 0;
            }
        }

        const std::size_t sentBytes =
            static_cast<std::size_t>(n);

        if (sentBytes < message.size())
        {
            outputBuffer_.Append(
                message.data() + sentBytes,
                message.size() - sentBytes);

            channel_->SetEvents(
                channel_->Events() | EPOLLOUT);

            loop_->UpdateChannel(
                channel_.get());
        }
        else
        {
            if (writeCompleteCallback_)
            {
                writeCompleteCallback_(
                    shared_from_this());
            }
        }
    }
}