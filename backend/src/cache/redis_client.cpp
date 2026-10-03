#include "jobpilot/cache/redis_client.h"

#include <chrono>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace jobpilot::core
{

    namespace
    {

        std::runtime_error MakeRedisError(
            const std::string &prefix,
            const redisContext *context)
        {
            std::string message = prefix;

            if (context != nullptr &&
                context->errstr != nullptr)
            {
                message += ": ";
                message += context->errstr;
            }

            return std::runtime_error(message);
        }

    } // namespace

    RedisClient::RedisClient(
        const std::string &host,
        std::uint16_t port,
        std::size_t maxConn,
        int timeoutMs)
        : host_(host),
          port_(port),
          maxConn_(maxConn),
          timeoutMs_(timeoutMs),
          idle_(),
          total_(0),
          mutex_(),
          condition_()
    {
        if (host_.empty())
        {
            throw std::invalid_argument(
                "Redis host must not be empty");
        }

        if (maxConn_ == 0)
        {
            throw std::invalid_argument(
                "Redis maxConn must be greater than zero");
        }

        if (timeoutMs_ <= 0)
        {
            throw std::invalid_argument(
                "Redis timeoutMs must be greater than zero");
        }
    }

    RedisClient::~RedisClient()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        while (!idle_.empty())
        {
            redisContext *connection = idle_.front();
            idle_.pop_front();

            if (connection != nullptr)
            {
                redisFree(connection);
            }
        }

        total_ = 0;
    }

    redisContext *RedisClient::CreateConnection() const
    {
        const auto timeoutSeconds =
            static_cast<time_t>(timeoutMs_ / 1000);

        const auto timeoutMicroseconds =
            static_cast<suseconds_t>(
                (timeoutMs_ % 1000) * 1000);

        timeval timeout{};
        timeout.tv_sec = timeoutSeconds;
        timeout.tv_usec = timeoutMicroseconds;

        redisContext *connection =
            redisConnectWithTimeout(
                host_.c_str(),
                port_,
                timeout);

        if (connection == nullptr)
        {
            throw std::runtime_error(
                "redisConnectWithTimeout returned null");
        }

        if (connection->err != 0)
        {
            std::runtime_error error =
                MakeRedisError(
                    "redis connect failed",
                    connection);

            redisFree(connection);
            throw error;
        }

        if (redisSetTimeout(
                connection,
                timeout) != REDIS_OK)
        {
            std::runtime_error error =
                MakeRedisError(
                    "redisSetTimeout failed",
                    connection);

            redisFree(connection);
            throw error;
        }

        void *rawReply =
            redisCommand(
                connection,
                "PING");

        if (rawReply == nullptr)
        {
            std::runtime_error error =
                MakeRedisError(
                    "redis PING failed",
                    connection);

            redisFree(connection);
            throw error;
        }

        redisReply *reply =
            static_cast<redisReply *>(rawReply);

        const bool success =
            reply->type == REDIS_REPLY_STATUS &&
            reply->str != nullptr &&
            std::string(reply->str) == "PONG";

        freeReplyObject(reply);

        if (!success)
        {
            redisFree(connection);

            throw std::runtime_error(
                "redis PING returned unexpected reply");
        }

        return connection;
    }

    bool RedisClient::CheckConnection(
        redisContext *connection) const
    {
        if (connection == nullptr)
        {
            return false;
        }

        if (connection->err != 0)
        {
            return false;
        }

        void *rawReply =
            redisCommand(
                connection,
                "PING");

        if (rawReply == nullptr)
        {
            return false;
        }

        redisReply *reply =
            static_cast<redisReply *>(rawReply);

        const bool success =
            reply->type == REDIS_REPLY_STATUS &&
            reply->str != nullptr &&
            std::string(reply->str) == "PONG";

        freeReplyObject(reply);

        if (!success)
        {
            return false;
        }

        return connection->err == 0;
    }

    redisContext *RedisClient::GetConnection()
    {
        std::unique_lock<std::mutex> lock(mutex_);

        while (true)
        {
            // -------------------------------------------------
            // 1. 优先获取空闲连接
            // -------------------------------------------------
            if (!idle_.empty())
            {
                redisContext *connection =
                    idle_.front();

                idle_.pop_front();

                if (connection != nullptr &&
                    CheckConnection(connection))
                {
                    return connection;
                }

                // 健康检查失败，销毁连接
                if (connection != nullptr)
                {
                    redisFree(connection);
                }

                if (total_ > 0)
                {
                    --total_;
                }

                // 继续尝试获取/创建连接
                continue;
            }

            // -------------------------------------------------
            // 2. 没有空闲连接，并且还没达到最大连接数
            // -------------------------------------------------
            if (total_ < maxConn_)
            {
                redisContext *connection =
                    CreateConnection();

                // 只有创建成功后才增加 total_
                ++total_;

                return connection;
            }

            // -------------------------------------------------
            // 3. 达到最大连接数
            //    等待其他线程 ReturnConnection()
            // -------------------------------------------------
            const auto waitDuration =
                std::chrono::milliseconds(timeoutMs_);

            const bool awakened =
                condition_.wait_for(
                    lock,
                    waitDuration,
                    [this]
                    {
                        return !idle_.empty() ||
                               total_ < maxConn_;
                    });

            if (!awakened)
            {
                throw std::runtime_error(
                    "Redis connection pool timeout");
            }
        }
    }

    void RedisClient::ReturnConnection(
        redisContext *connection)
    {
        if (connection == nullptr)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // 健康连接重新放回连接池
        if (CheckConnection(connection))
        {
            idle_.push_back(connection);
        }
        else
        {
            // 坏连接直接销毁
            redisFree(connection);

            if (total_ > 0)
            {
                --total_;
            }
        }

        // 唤醒等待中的线程
        condition_.notify_one();
    }

    redisContext *RedisClient::Reconnect()
    {
        return GetConnection();
    }

    std::optional<std::string> RedisClient::Get(
        const std::string &key)
    {
        redisContext *connection =
            GetConnection();

        try
        {
            void *rawReply =
                redisCommand(
                    connection,
                    "GET %s",
                    key.c_str());

            if (rawReply == nullptr)
            {
                // 当前连接已经失效
                {
                    std::lock_guard<std::mutex> lock(
                        mutex_);

                    redisFree(connection);

                    if (total_ > 0)
                    {
                        --total_;
                    }

                    condition_.notify_one();
                }

                // 获取一个新连接重试一次
                connection = Reconnect();

                rawReply =
                    redisCommand(
                        connection,
                        "GET %s",
                        key.c_str());

                if (rawReply == nullptr)
                {
                    std::lock_guard<std::mutex> lock(
                        mutex_);

                    redisFree(connection);

                    if (total_ > 0)
                    {
                        --total_;
                    }

                    condition_.notify_one();

                    throw MakeRedisError(
                        "Redis GET failed after reconnect",
                        nullptr);
                }
            }

            std::unique_ptr<
                redisReply,
                decltype(&freeReplyObject)>
                reply(
                    static_cast<redisReply *>(rawReply),
                    &freeReplyObject);

            if (reply->type == REDIS_REPLY_NIL)
            {
                ReturnConnection(connection);
                return std::nullopt;
            }

            if (reply->type != REDIS_REPLY_STRING ||
                reply->str == nullptr)
            {
                ReturnConnection(connection);

                throw std::runtime_error(
                    "Redis GET returned unexpected reply");
            }

            std::string value(
                reply->str,
                static_cast<std::size_t>(
                    reply->len));

            ReturnConnection(connection);

            return value;
        }
        catch (...)
        {
            // 如果连接仍然由本次函数持有，
            // 这里需要避免重复归还。
            // 正常 redisCommand 失败路径已经处理连接。
            throw;
        }
    }

    bool RedisClient::Set(
        const std::string &key,
        const std::string &value)
    {
        redisContext *connection =
            GetConnection();

        void *rawReply =
            redisCommand(
                connection,
                "SET %s %s",
                key.c_str(),
                value.c_str());

        if (rawReply == nullptr)
        {
            {
                std::lock_guard<std::mutex> lock(
                    mutex_);

                redisFree(connection);

                if (total_ > 0)
                {
                    --total_;
                }

                condition_.notify_one();
            }

            connection = Reconnect();

            rawReply =
                redisCommand(
                    connection,
                    "SET %s %s",
                    key.c_str(),
                    value.c_str());

            if (rawReply == nullptr)
            {
                {
                    std::lock_guard<std::mutex> lock(
                        mutex_);

                    redisFree(connection);

                    if (total_ > 0)
                    {
                        --total_;
                    }

                    condition_.notify_one();
                }

                throw MakeRedisError(
                    "Redis SET failed after reconnect",
                    nullptr);
            }
        }

        std::unique_ptr<
            redisReply,
            decltype(&freeReplyObject)>
            reply(
                static_cast<redisReply *>(rawReply),
                &freeReplyObject);

        const bool success =
            reply->type == REDIS_REPLY_STATUS &&
            reply->str != nullptr &&
            std::string(reply->str) == "OK";

        ReturnConnection(connection);

        if (!success)
        {
            throw std::runtime_error(
                "Redis SET returned unexpected reply");
        }

        return true;
    }

    bool RedisClient::SetEx(
        const std::string &key,
        std::int64_t seconds,
        const std::string &value)
    {
        if (seconds <= 0)
        {
            throw std::invalid_argument(
                "Redis SETEX seconds must be greater than zero");
        }

        redisContext *connection =
            GetConnection();

        const long long ttl =
            static_cast<long long>(seconds);

        void *rawReply =
            redisCommand(
                connection,
                "SETEX %s %lld %s",
                key.c_str(),
                ttl,
                value.c_str());

        if (rawReply == nullptr)
        {
            {
                std::lock_guard<std::mutex> lock(
                    mutex_);

                redisFree(connection);

                if (total_ > 0)
                {
                    --total_;
                }

                condition_.notify_one();
            }

            connection = Reconnect();

            rawReply =
                redisCommand(
                    connection,
                    "SETEX %s %lld %s",
                    key.c_str(),
                    ttl,
                    value.c_str());

            if (rawReply == nullptr)
            {
                {
                    std::lock_guard<std::mutex> lock(
                        mutex_);

                    redisFree(connection);

                    if (total_ > 0)
                    {
                        --total_;
                    }

                    condition_.notify_one();
                }

                throw MakeRedisError(
                    "Redis SETEX failed after reconnect",
                    nullptr);
            }
        }

        std::unique_ptr<
            redisReply,
            decltype(&freeReplyObject)>
            reply(
                static_cast<redisReply *>(rawReply),
                &freeReplyObject);

        const bool success =
            reply->type == REDIS_REPLY_STATUS &&
            reply->str != nullptr &&
            std::string(reply->str) == "OK";

        ReturnConnection(connection);

        if (!success)
        {
            throw std::runtime_error(
                "Redis SETEX returned unexpected reply");
        }

        return true;
    }

    bool RedisClient::Del(
        const std::string &key)
    {
        redisContext *connection =
            GetConnection();

        void *rawReply =
            redisCommand(
                connection,
                "DEL %s",
                key.c_str());

        if (rawReply == nullptr)
        {
            {
                std::lock_guard<std::mutex> lock(
                    mutex_);

                redisFree(connection);

                if (total_ > 0)
                {
                    --total_;
                }

                condition_.notify_one();
            }

            connection = Reconnect();

            rawReply =
                redisCommand(
                    connection,
                    "DEL %s",
                    key.c_str());

            if (rawReply == nullptr)
            {
                {
                    std::lock_guard<std::mutex> lock(
                        mutex_);

                    redisFree(connection);

                    if (total_ > 0)
                    {
                        --total_;
                    }

                    condition_.notify_one();
                }

                throw MakeRedisError(
                    "Redis DEL failed after reconnect",
                    nullptr);
            }
        }

        std::unique_ptr<
            redisReply,
            decltype(&freeReplyObject)>
            reply(
                static_cast<redisReply *>(rawReply),
                &freeReplyObject);

        if (reply->type != REDIS_REPLY_INTEGER)
        {
            ReturnConnection(connection);

            throw std::runtime_error(
                "Redis DEL returned unexpected reply");
        }

        ReturnConnection(connection);

        return true;
    }

    std::int64_t RedisClient::Incr(
        const std::string &key,
        std::int64_t delta)
    {
        const long long increment =
            static_cast<long long>(delta);

        redisContext *connection =
            GetConnection();

        void *rawReply =
            redisCommand(
                connection,
                "INCRBY %s %lld",
                key.c_str(),
                increment);

        if (rawReply == nullptr)
        {
            {
                std::lock_guard<std::mutex> lock(
                    mutex_);

                redisFree(connection);

                if (total_ > 0)
                {
                    --total_;
                }

                condition_.notify_one();
            }

            connection = Reconnect();

            rawReply =
                redisCommand(
                    connection,
                    "INCRBY %s %lld",
                    key.c_str(),
                    increment);

            if (rawReply == nullptr)
            {
                {
                    std::lock_guard<std::mutex> lock(
                        mutex_);

                    redisFree(connection);

                    if (total_ > 0)
                    {
                        --total_;
                    }

                    condition_.notify_one();
                }

                throw MakeRedisError(
                    "Redis INCRBY failed after reconnect",
                    nullptr);
            }
        }

        std::unique_ptr<
            redisReply,
            decltype(&freeReplyObject)>
            reply(
                static_cast<redisReply *>(rawReply),
                &freeReplyObject);

        if (reply->type != REDIS_REPLY_INTEGER)
        {
            ReturnConnection(connection);

            throw std::runtime_error(
                "Redis INCRBY returned unexpected reply");
        }

        const std::int64_t result =
            static_cast<std::int64_t>(
                reply->integer);

        ReturnConnection(connection);

        return result;
    }

} // namespace jobpilot::core