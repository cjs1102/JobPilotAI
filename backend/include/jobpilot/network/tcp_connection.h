// 管理一个 TCP socket
// 管理这个 socket 对应的 Channel
// 管理输入 Buffer
// 管理输出 Buffer
// 管理连接状态
// 处理读事件
// 处理写事件
// 处理连接关闭
// 把网络事件通过 Callback 通知上层
#pragma once // 当前头文件在同一个编译单元中只处理一次，防止重复包含同一个头文件

#include <functional> //<...> 和 "..." 的区别
                      //尖括号通常用于：系统头文件；编译器配置的系统包含目录，C++ 标准库；
#include <memory>     //智能指针和对象生命周期

#include <string>

#include "jobpilot/network/buffer.h"

namespace jobpilot::core
{
    class EventLoop;
    class Channel;

    class TcpConnection
        : public std::enable_shared_from_this<TcpConnection> // 可以获得当前对象对应的 shared_ptr。auto self = shared_from_this();
    {
    public:
        enum class State // 强类型枚举
        {
            kConnecting,
            kConnected,
            kDisconnecting,
            kDisconnected
        };

        using Ptr = std::shared_ptr<TcpConnection>; // 类型别名的语法，给复杂类型起一个短名字

        // std::function<返回类型(参数列表)> 保存可调用对象的通用包装器。
        using ConnectionCallback =
            std::function<void(const Ptr &)>;

        using MessageCallback = // 当 TcpConnection 收到数据后，把“连接对象”和“输入缓冲区”交给业务层。
            std::function<void(
                const Ptr &,
                Buffer *)>;

        using WriteCompleteCallback = // 输出数据全部写完之后通知上层。
            std::function<void(const Ptr &)>;

        using CloseCallback =
            std::function<void(const Ptr &)>;

        TcpConnection(
            EventLoop *loop,  // 连接属于哪个EventLoop
            std::string name, // 连接名字
            int socketFd);

        ~TcpConnection();

        TcpConnection(const TcpConnection &) = delete;
        TcpConnection &operator=(const TcpConnection &) = delete;

        EventLoop *GetLoop() const; // 获取这个连接所属的EventLoop
        const std::string &Name() const;
        int Fd() const;
        State GetState() const;
        bool IsConnected() const;
        bool IsDisconnected() const;

        void EstablishConnection();            // TCP连接正式进入已连接状态后的初始化工作
        void Send(const std::string &message); // 对外提供的发送接口
        void Shutdown();                       // 半关闭，关闭socket的某个通信方向

        void SetConnectionCallback(
            const ConnectionCallback &callback);

        void SetMessageCallback(
            const MessageCallback &callback);

        void SetWriteCompleteCallback(
            const WriteCompleteCallback &callback);

        void SetCloseCallback(
            const CloseCallback &callback);

        void HandleRead();
        void HandleWrite();
        void HandleClose();

        void ConnectEstablished();
        void ConnectDestroyed();

    private:
        void SendInLoop(const std::string &message);
        void ShutdownInLoop();
        void SetState(State state);

    private:
        EventLoop *loop_;
        std::string name_;
        int socketFd_;
        std::unique_ptr<Channel> channel_;

        Buffer inputBuffer_;
        Buffer outputBuffer_;

        State state_;

        ConnectionCallback connectionCallback_;       // 链接状态变化
        MessageCallback messageCallback_;             // 收到数据
        WriteCompleteCallback writeCompleteCallback_; // 数据发送完成
        CloseCallback closeCallback_;                 // 连接关闭
    };
}