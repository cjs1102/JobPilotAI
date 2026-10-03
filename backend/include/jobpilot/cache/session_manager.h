#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace jobpilot::core
{
    class RedisClient;

    class SessionManager
    {
    public:
        static constexpr std::int64_t kDefaultTtlSeconds =
            86400;

        explicit SessionManager(
            RedisClient &redis,
            std::int64_t ttlSeconds =
                kDefaultTtlSeconds);

        void Set(
            const std::string &token,
            std::uint64_t userId);

        std::optional<std::uint64_t> GetUserId(
            const std::string &token);

        void Remove(
            const std::string &token);

    private:
        std::string BuildKey(
            const std::string &token) const;

    private:
        RedisClient &redis_;
        std::int64_t ttlSeconds_;
    };
}