#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <hiredis/hiredis.h>

namespace jobpilot::core
{

    class RedisClient
    {
    public:
        RedisClient(
            const std::string &host,
            std::uint16_t port,
            std::size_t maxConn = 4,
            int timeoutMs = 1000);

        ~RedisClient();

        RedisClient(const RedisClient &) = delete;
        RedisClient &operator=(const RedisClient &) = delete;

        std::optional<std::string> Get(const std::string &key);

        bool Set(
            const std::string &key,
            const std::string &value);

        bool SetEx(
            const std::string &key,
            std::int64_t seconds,
            const std::string &value);

        bool Del(const std::string &key);

        std::int64_t Incr(
            const std::string &key,
            std::int64_t delta = 1);

    private:
        redisContext *GetConnection();

        void ReturnConnection(
            redisContext *connection);

        redisContext *CreateConnection() const;

        bool CheckConnection(
            redisContext *connection) const;

        redisContext *Reconnect();

    private:
        std::string host_;
        std::uint16_t port_;

        // 最大连接数
        std::size_t maxConn_;

        // Redis socket / pool timeout
        int timeoutMs_;

        // 空闲连接
        std::deque<redisContext *> idle_;

        // 已经创建出来的连接总数：
        // idle_ + 正在被线程使用的连接
        std::size_t total_;

        std::mutex mutex_;

        // 连接池耗尽时等待可用连接
        std::condition_variable condition_;
    };

} // namespace jobpilot::core