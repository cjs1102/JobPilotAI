#include "jobpilot/http/http_server.h"
#include "jobpilot/network/event_loop.h"

#include <stdexcept>

namespace jobpilot::core
{

    HttpServer::HttpServer(
        EventLoop *loop,
        const std::string &ip,
        uint16_t port,
        std::size_t maxRequestBodyBytes)
        : loop_(loop),
          tcpServer_(loop, ip, port),
          router_(),
          maxRequestBodyBytes_(maxRequestBodyBytes),
          contexts_(),
          threadPool_(4)
    {
        if (maxRequestBodyBytes_ == 0)
        {
            throw std::invalid_argument(
                "max request body size must be greater than zero");
        }

        tcpServer_.SetConnectionCallback(
            [this](const TcpConnection::Ptr &connection)
            {
                HandleConnection(connection);
            });

        tcpServer_.SetMessageCallback(
            [this](
                const TcpConnection::Ptr &connection,
                Buffer *buffer)
            {
                HandleMessage(connection, buffer);
            });

        tcpServer_.SetWriteCompleteCallback(
            [this](const TcpConnection::Ptr &connection)
            {
                HandleWriteComplete(connection);
            });

        tcpServer_.SetCloseCallback(
            [this](const TcpConnection::Ptr &connection)
            {
                HandleClose(connection);
            });
    }

    HttpServer::~HttpServer()
    {
        Stop();
    }

    void HttpServer::Start()
    {
        tcpServer_.Start();
    }

    void HttpServer::Stop()
    {
        tcpServer_.Stop();

        for (auto &entry : contexts_)
        {
            entry.second->closed = true;
            entry.second->pendingResponses.clear();
        }

        contexts_.clear();
    }

    HttpRouter &HttpServer::Router()
    {
        return router_;
    }

    void HttpServer::HandleConnection(
        const TcpConnection::Ptr &connection)
    {
        if (!connection)
        {
            return;
        }

        contexts_[connection->Name()] =
            std::make_shared<ConnectionContext>(
                maxRequestBodyBytes_);
    }

    void HttpServer::HandleMessage(
        const TcpConnection::Ptr &connection,
        Buffer *buffer)
    {
        auto contextIt = contexts_.find(connection->Name());
        if (contextIt == contexts_.end())
        {
            return;
        }

        const auto &context = contextIt->second;

        while (buffer->ReadableBytes() > 0)
        {
            ParseResult result =
                context->parser.Parse(buffer);

            if (result ==
                ParseResult::kIncomplete)
            {
                return;
            }

            const std::size_t sequence =
                context->nextRequestSequence++;

            if (result ==
                ParseResult::kError)
            {
                HttpResponse response;
                response.SetStatusCode(400);
                response.SetStatusMessage("Bad Request");
                response.SetKeepAlive(false);
                response.SetBody(
                    "{\"code\":400,\"message\":\"bad request\"}");

                context->pendingResponses.emplace(
                    sequence,
                    PendingResponse{
                        response.Serialize(),
                        false});

                context->parser.Reset();

                FlushPendingResponses(connection, context);
                return;
            }

            HttpRequest request =
                context->parser.Request();

            const bool requestKeepAlive =
                request.KeepAlive();

            context->parser.Reset();

            EventLoop *loop = loop_;
            ThreadPool *threadPool = &threadPool_;
            HttpRouter *router = &router_;

            threadPool->Submit(
                [loop,
                 connection,
                 context,
                 sequence,
                 router,
                 request = std::move(request)]() mutable
                {
                    HttpResponse response;
                    response.SetKeepAlive(request.KeepAlive());

                    if (!router->Route(request, &response))
                    {
                        response.SetStatusCode(404);
                        response.SetStatusMessage("Not Found");
                        response.SetKeepAlive(
                            request.KeepAlive());
                        response.SetBody(
                            "{\"code\":404,"
                            "\"message\":\"not found\"}");
                    }

                    std::string serialized =
                        response.Serialize();

                    const bool keepAlive =
                        response.KeepAlive();

                    loop->RunInLoop(
                        [connection,
                         context,
                         sequence,
                         serialized =
                             std::move(serialized),
                         keepAlive]() mutable
                        {
                            if (context->closed)
                            {
                                return;
                            }

                            if (context->hasCloseSequence &&
                                sequence >
                                    context->closeSequence)
                            {
                                return;
                            }

                            context->pendingResponses.emplace(
                                sequence,
                                PendingResponse{
                                    std::move(serialized),
                                    keepAlive});

                            HttpServer::FlushPendingResponses(
                                connection,
                                context);
                        });
                });

            /*
             * 一个请求已经提交给 worker。
             *
             * 继续 while：
             * 如果 Buffer 里还有完整请求，
             * 就继续提交。
             *
             * 这样多个客户端之间可以真正并发，
             * 同一个连接的多个请求则通过 sequence
             * 保证响应顺序。
             */

            (void)requestKeepAlive;
        }
    }

    void HttpServer::HandleWriteComplete(
        const TcpConnection::Ptr &connection)
    {
        auto it =
            contexts_.find(connection->Name());

        if (it == contexts_.end())
        {
            return;
        }

        if (it->second->closeAfterWrite)
        {
            connection->Shutdown();
        }
    }

    void HttpServer::HandleClose(
        const TcpConnection::Ptr &connection)
    {
        auto it =
            contexts_.find(connection->Name());

        if (it == contexts_.end())
        {
            return;
        }

        it->second->closed = true;
        it->second->pendingResponses.clear();

        contexts_.erase(it);
    }

    void HttpServer::SendBadRequest(
        const TcpConnection::Ptr &connection)
    {
        auto it =
            contexts_.find(connection->Name());

        if (it == contexts_.end())
        {
            return;
        }

        HttpResponse response;
        response.SetStatusCode(400);
        response.SetStatusMessage("Bad Request");
        response.SetKeepAlive(false);
        response.SetBody(
            "{\"code\":400,\"message\":\"bad request\"}");

        const auto &context = it->second;

        const std::size_t sequence =
            context->nextRequestSequence++;

        context->pendingResponses.emplace(
            sequence,
            PendingResponse{
                response.Serialize(),
                false});

        FlushPendingResponses(
            connection,
            context);
    }

        void HttpServer::FlushPendingResponses(
        const TcpConnection::Ptr &connection,
        const std::shared_ptr<ConnectionContext> &context)
    {
        if (context->closed)
        {
            return;
        }

        while (true)
        {
            if (context->hasCloseSequence &&
                context->nextResponseSequence >
                    context->closeSequence)
            {
                context->pendingResponses.clear();
                return;
            }

            auto it =
                context->pendingResponses.find(
                    context->nextResponseSequence);

            if (it == context->pendingResponses.end())
            {
                return;
            }

            PendingResponse response =
                std::move(it->second);

            context->pendingResponses.erase(it);

            if (!response.keepAlive)
            {
                /*
                 * 必须在 Send 之前设置。
                 *
                 * 避免 Send 之后立刻触发
                 * WriteComplete 时还没有设置关闭标志。
                 */
                context->closeAfterWrite = true;
                context->hasCloseSequence = true;
                context->closeSequence =
                    context->nextResponseSequence;
            }

            connection->Send(response.serialized);

            ++context->nextResponseSequence;

            if (context->closeAfterWrite)
            {
                /*
                 * 当前响应是不保持连接的，
                 * 后面的 pipeline 响应全部丢弃。
                 */
                context->pendingResponses.clear();
                return;
            }
        }
    }

} // namespace jobpilot::core
