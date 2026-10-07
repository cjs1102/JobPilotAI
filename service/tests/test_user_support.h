#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

#include <unistd.h>

#include "jobpilot/cache/redis_client.h"
#include "jobpilot/database/mysql_pool.h"

namespace jobpilot::test
{
    inline std::string GetEnvironment(
        const char *name,
        const std::string &defaultValue)
    {
        const char *value = std::getenv(name);

        if (value == nullptr || *value == '\0')
        {
            return defaultValue;
        }

        return value;
    }

    inline std::uint16_t GetPort(
        const char *name,
        std::uint16_t defaultValue)
    {
        const std::string value =
            GetEnvironment(
                name,
                std::to_string(defaultValue));

        const unsigned long port =
            std::stoul(value);

        if (port == 0 || port > 65535)
        {
            throw std::invalid_argument(
                std::string(name) +
                " must be between 1 and 65535");
        }

        return static_cast<std::uint16_t>(port);
    }

    inline std::unique_ptr<
        jobpilot::core::ConnectionPool>
    MakeConnectionPool(
        std::size_t maxConnections = 4)
    {
        const char *password =
            std::getenv("MYSQL_PASSWORD");

        if (password == nullptr || *password == '\0')
        {
            throw std::runtime_error(
                "MYSQL_PASSWORD is not set");
        }

        return std::make_unique<
            jobpilot::core::ConnectionPool>(
            GetEnvironment(
                "MYSQL_HOST",
                "127.0.0.1"),
            GetPort(
                "MYSQL_PORT",
                3306),
            GetEnvironment(
                "MYSQL_USER",
                "root"),
            password,
            GetEnvironment(
                "MYSQL_DATABASE",
                "jobpilot"),
            maxConnections);
    }

    inline std::unique_ptr<jobpilot::core::RedisClient>
    MakeRedisClient()
    {
        return std::make_unique<
            jobpilot::core::RedisClient>(
            GetEnvironment(
                "REDIS_HOST",
                "127.0.0.1"),
            GetPort(
                "REDIS_PORT",
                6379),
            4,
            1000);
    }

    inline std::string UniqueEmail(
        const std::string &prefix)
    {
        static std::atomic<std::uint64_t> counter{0};

        const std::uint64_t value =
            counter.fetch_add(
                1,
                std::memory_order_relaxed);

        return prefix +
               std::to_string(getpid()) +
               "-" +
               std::to_string(value) +
               "@example.com";
    }

    inline void CleanupUser(
        jobpilot::core::ConnectionPool *pool,
        const std::string &email)
    {
        if (pool == nullptr || email.empty())
        {
            return;
        }

        const jobpilot::core::Result result =
            pool->Query(
                "SELECT id FROM users WHERE email = ? LIMIT 1",
                {email});

        if (result.rowCount() == 1)
        {
            const auto &idValue =
                result.value(0, 0);

            if (!idValue.has_value())
            {
                throw std::runtime_error(
                    "user id must not be NULL");
            }

            const std::uint64_t userId =
                static_cast<std::uint64_t>(
                    std::stoull(
                        idValue.value()));

            pool->Execute(
                "DELETE FROM applications WHERE user_id = ?",
                {userId});

            pool->Execute(
                "DELETE rv FROM resume_versions rv "
                "JOIN resumes r ON r.id = rv.resume_id "
                "WHERE r.user_id = ?",
                {userId});

            pool->Execute(
                "DELETE FROM resumes WHERE user_id = ?",
                {userId});

            pool->Execute(
                "DELETE FROM skills WHERE user_id = ?",
                {userId});

            pool->Execute(
                "DELETE FROM profiles WHERE user_id = ?",
                {userId});
        }

        pool->Execute(
            "DELETE FROM users WHERE email = ?",
            {email});
    }

    inline std::string RequireValue(
        const jobpilot::core::Result::Value &value)
    {
        if (!value.has_value())
        {
            throw std::runtime_error(
                "expected non-null query value");
        }

        return value.value();
    }
} // namespace jobpilot::test
