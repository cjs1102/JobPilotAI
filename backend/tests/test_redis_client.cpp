#include "jobpilot/cache/redis_client.h"
#include "jobpilot/cache/session_manager.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    void Check(
        bool condition,
        const std::string &message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    bool WaitUntilKeyMissing(
        jobpilot::core::RedisClient &redis,
        const std::string &key,
        std::chrono::milliseconds timeout)
    {
        const auto deadline =
            std::chrono::steady_clock::now() + timeout;

        while (std::chrono::steady_clock::now() < deadline)
        {
            if (!redis.Get(key).has_value())
            {
                return true;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(50));
        }

        return !redis.Get(key).has_value();
    }

    std::uint16_t GetRedisPort()
    {
        const char *value =
            std::getenv("REDIS_PORT");

        if (value == nullptr ||
            *value == '\0')
        {
            return 6379;
        }

        const unsigned long port =
            std::stoul(value);

        if (port == 0 || port > 65535)
        {
            throw std::invalid_argument(
                "REDIS_PORT must be between 1 and 65535");
        }

        return static_cast<std::uint16_t>(
            port);
    }

    std::string GetRedisHost()
    {
        const char *value =
            std::getenv("REDIS_HOST");

        if (value == nullptr ||
            *value == '\0')
        {
            return "127.0.0.1";
        }

        return value;
    }

    void TestSetAndGet(
        jobpilot::core::RedisClient &redis)
    {
        const std::string key =
            "jobpilot:test:set_get";

        Check(
            redis.Del(key),
            "DEL before SET failed");

        Check(
            redis.Set(
                key,
                "hello"),
            "SET failed");

        const auto value =
            redis.Get(key);

        Check(
            value.has_value(),
            "GET should return a value");

        Check(
            value.value() == "hello",
            "GET returned unexpected value");

        Check(
            redis.Del(key),
            "DEL failed");

        const auto deletedValue =
            redis.Get(key);

        Check(
            !deletedValue.has_value(),
            "value should not exist after DEL");
    }

    void TestEmptyString(
        jobpilot::core::RedisClient &redis)
    {
        const std::string key =
            "jobpilot:test:empty";

        Check(
            redis.Set(key, ""),
            "SET empty string failed");

        const auto value =
            redis.Get(key);

        Check(
            value.has_value(),
            "empty string should still be present");

        Check(
            value->empty(),
            "empty string was not preserved");

        Check(
            redis.Del(key),
            "DEL empty string failed");
    }

    void TestSetExAndExpiration(
        jobpilot::core::RedisClient &redis)
    {
        const std::string key =
            "jobpilot:test:setex";

        Check(
            redis.Del(key),
            "DEL before SETEX failed");

        Check(
            redis.SetEx(
                key,
                2,
                "temporary"),
            "SETEX failed");

        const auto value =
            redis.Get(key);

        Check(
            value.has_value(),
            "SETEX value should exist immediately");

        Check(
            value.value() == "temporary",
            "SETEX returned unexpected value");

        Check(
            WaitUntilKeyMissing(
                redis,
                key,
                std::chrono::seconds(5)),
            "SETEX value should expire");
    }

    void TestIncr(
        jobpilot::core::RedisClient &redis)
    {
        const std::string key =
            "jobpilot:test:counter";

        Check(
            redis.Del(key),
            "DEL before INCR failed");

        const std::int64_t first =
            redis.Incr(key);

        Check(
            first == 1,
            "first INCR should return 1");

        const std::int64_t second =
            redis.Incr(key);

        Check(
            second == 2,
            "second INCR should return 2");

        const std::int64_t fifth =
            redis.Incr(key, 3);

        Check(
            fifth == 5,
            "INCRBY 3 should return 5");

        Check(
            redis.Del(key),
            "DEL counter failed");
    }

    void TestConcurrentAccess(
        jobpilot::core::RedisClient &redis)
    {
        constexpr int kThreadCount = 8;
        constexpr int kOperationsPerThread = 20;

        std::vector<std::thread> threads;
        threads.reserve(kThreadCount);

        std::mutex errorMutex;
        std::string firstError;

        for (int threadIndex = 0;
             threadIndex < kThreadCount;
             ++threadIndex)
        {
            threads.emplace_back(
                [&redis,
                 &errorMutex,
                 &firstError,
                 threadIndex]()
                {
                    try
                    {
                        for (int i = 0;
                             i < kOperationsPerThread;
                             ++i)
                        {
                            const std::string key =
                                "jobpilot:test:concurrent:" +
                                std::to_string(
                                    threadIndex) +
                                ":" +
                                std::to_string(i);

                            const std::string value =
                                "thread-" +
                                std::to_string(
                                    threadIndex) +
                                "-" +
                                std::to_string(i);

                            Check(
                                redis.Set(
                                    key,
                                    value),
                                "concurrent SET failed");

                            const auto result =
                                redis.Get(key);

                            Check(
                                result.has_value(),
                                "concurrent GET returned NIL");

                            Check(
                                result.value() == value,
                                "concurrent GET value mismatch");

                            Check(
                                redis.Del(key),
                                "concurrent DEL failed");
                        }
                    }
                    catch (const std::exception &e)
                    {
                        std::lock_guard<
                            std::mutex>
                            lock(
                                errorMutex);

                        if (firstError.empty())
                        {
                            firstError = e.what();
                        }
                    }
                });
        }

        for (std::thread &thread : threads)
        {
            thread.join();
        }

        Check(
            firstError.empty(),
            "concurrent Redis test failed: " +
                firstError);
    }

    void TestSession(
        jobpilot::core::RedisClient &redis)
    {
        jobpilot::core::SessionManager session(
            redis,
            2);

        const std::string token =
            "jobpilot:test:session";

        session.Remove(token);

        session.Set(
            token,
            static_cast<std::uint64_t>(123456));

        const auto userId =
            session.GetUserId(token);

        Check(
            userId.has_value(),
            "session should return userId");

        Check(
            userId.value() ==
                static_cast<std::uint64_t>(123456),
            "session userId mismatch");

        Check(
            WaitUntilKeyMissing(
                redis,
                "session:" + token,
                std::chrono::seconds(5)),
            "session should expire");

        session.Remove(token);
    }
}

int main()
{
    try
    {
        const std::string host =
            GetRedisHost();

        const std::uint16_t port =
            GetRedisPort();

        jobpilot::core::RedisClient redis(
            host,
            port,
            4,
            1000);

        TestSetAndGet(redis);

        std::cout
            << "SET/GET/DEL test passed"
            << std::endl;

        TestEmptyString(redis);

        std::cout
            << "empty string test passed"
            << std::endl;

        TestSetExAndExpiration(redis);

        std::cout
            << "SETEX expiration test passed"
            << std::endl;

        TestIncr(redis);

        std::cout
            << "INCR test passed"
            << std::endl;

        TestConcurrentAccess(redis);

        std::cout
            << "8-thread concurrent Redis test passed"
            << std::endl;

        TestSession(redis);

        std::cout
            << "Session test passed"
            << std::endl;

        std::cout
            << "redis client test passed"
            << std::endl;

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr
            << "redis client test failed: "
            << e.what()
            << std::endl;

        return 1;
    }
}
