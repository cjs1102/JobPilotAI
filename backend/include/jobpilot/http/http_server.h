#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "jobpilot/network/buffer.h"
#include "jobpilot/network/tcp_connection.h"

#include "jobpilot/http/http_request_parser.h"
#include "jobpilot/http/http_router.h"
#include "jobpilot/network/tcp_server.h"

#include "jobpilot/core/thread_pool.h"

namespace jobpilot::core
{

    class HttpServer
    {
    public:
        HttpServer(
            EventLoop *loop,
            const std::string &ip,
            uint16_t port);

        ~HttpServer();

        HttpServer(const HttpServer &) = delete;
        HttpServer &operator=(const HttpServer &) = delete;

        void Start();
        void Stop();

        HttpRouter &Router();

    private:
        struct PendingResponse
        {
            std::string serialized;
            bool keepAlive;
        };

        struct ConnectionContext
        {
            HttpRequestParser parser;

            std::size_t nextRequestSequence{0};
            std::size_t nextResponseSequence{0};

            std::unordered_map<std::size_t, PendingResponse> pendingResponses;

            bool closeAfterWrite{false};
            bool closed{false};

            bool hasCloseSequence{false};
            std::size_t closeSequence{0};
        };

        static void FlushPendingResponses(
            const TcpConnection::Ptr &connection,
            const std::shared_ptr<ConnectionContext> &context);

        void HandleConnection(
            const TcpConnection::Ptr &connection);

        void HandleMessage(
            const TcpConnection::Ptr &connection,
            Buffer *buffer);

        void HandleWriteComplete(
            const TcpConnection::Ptr &connection);

        void HandleClose(
            const TcpConnection::Ptr &connection);

        void SendBadRequest(
            const TcpConnection::Ptr &connection);

        private:
        EventLoop *loop_;

        TcpServer tcpServer_;
        HttpRouter router_;

        std::unordered_map<std::string,
                           std::shared_ptr<ConnectionContext>>
            contexts_;
        ThreadPool threadPool_;
    };

} // namespace jobpilot::core