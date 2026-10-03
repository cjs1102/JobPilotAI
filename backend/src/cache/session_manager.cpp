#include "jobpilot/cache/session_manager.h"

#include <limits>
#include <stdexcept>
#include <string>

#include "jobpilot/cache/redis_client.h"

namespace jobpilot::core
{
    SessionManager::SessionManager(
        RedisClient &redis,
        std::int64_t ttlSeconds)
        : redis_(redis),
          ttlSeconds_(ttlSeconds)
    {
        if (ttlSeconds_ <= 0)
        {
            throw std::invalid_argument(
                "Session TTL must be greater than zero");
        }
    }

    std::string SessionManager::BuildKey(
        const std::string &token) const
    {
        return "session:" + token;
    }

    void SessionManager::Set(
        const std::string &token,
        std::uint64_t userId)
    {
        const std::string key =
            BuildKey(token);

        const std::string value =
            std::to_string(userId);

        if (!redis_.SetEx(
                key,
                ttlSeconds_,
                value))
        {
            throw std::runtime_error(
                "failed to store session");
        }
    }

    std::optional<std::uint64_t>
    SessionManager::GetUserId(
        const std::string &token)
    {
        const std::optional<std::string>
            value =
                redis_.Get(BuildKey(token));

        if (!value.has_value())
        {
            return std::nullopt;
        }

        try
        {
            std::size_t parsed = 0;

            const unsigned long long userId =
                std::stoull(
                    value.value(),
                    &parsed);

            if (parsed != value.value().size())
            {
                throw std::invalid_argument(
                    "session userId contains invalid characters");
            }

            if (userId >
                std::numeric_limits<std::uint64_t>::max())
            {
                throw std::out_of_range(
                    "session userId is too large");
            }

            return static_cast<std::uint64_t>(
                userId);
        }
        catch (const std::exception &e)
        {
            throw std::runtime_error(
                std::string(
                    "invalid session userId: ") +
                e.what());
        }
    }

    void SessionManager::Remove(
        const std::string &token)
    {
        redis_.Del(
            BuildKey(token));
    }

} // namespace jobpilot::core