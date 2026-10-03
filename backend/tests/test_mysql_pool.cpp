#include "jobpilot/database/mysql_pool.h"

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    std::string GetEnvOrDefault(
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

    std::uint16_t GetPort()
    {
        const char *value =
            std::getenv("MYSQL_PORT");

        if (value == nullptr || *value == '\0')
        {
            return 3306;
        }

        const unsigned long port =
            std::stoul(value);

        if (port == 0 || port > 65535)
        {
            throw std::invalid_argument(
                "MYSQL_PORT must be between 1 and 65535");
        }

        return static_cast<std::uint16_t>(port);
    }

    void Check(
        bool condition,
        const std::string &message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void TestBasicQuery(
        jobpilot::core::ConnectionPool &pool)
    {
        const jobpilot::core::Result result =
            pool.Query(
                "SELECT ? AS int_value, "
                "? AS uint_value, "
                "? AS double_value, "
                "? AS string_value, "
                "? AS empty_value",
                {std::int64_t(-123),
                 std::uint64_t(123456),
                 3.1415926,
                 std::string("hello"),
                 std::string("")});

        Check(
            result.rowCount() == 1,
            "basic query row count mismatch");

        Check(
            result.columnCount() == 5,
            "basic query column count mismatch");

        Check(
            result.columnNames().size() == 5,
            "basic query column name count mismatch");

        Check(
            result.value(0, 0).has_value() &&
                *result.value(0, 0) == "-123",
            "int64 parameter result mismatch");

        Check(
            result.value(0, 1).has_value() &&
                *result.value(0, 1) == "123456",
            "uint64 parameter result mismatch");

        Check(
            result.value(0, 2).has_value(),
            "double parameter result is null");

        Check(
            result.value(0, 3).has_value() &&
                *result.value(0, 3) == "hello",
            "string parameter result mismatch");

        Check(
            result.value(0, 4).has_value() &&
                result.value(0, 4)->empty(),
            "empty string parameter result mismatch");
    }

    void TestNullQuery(
        jobpilot::core::ConnectionPool &pool)
    {
        const jobpilot::core::Result result =
            pool.Query(
                "SELECT ? AS null_value",
                {nullptr});

        Check(
            result.rowCount() == 1,
            "NULL query row count mismatch");

        Check(
            result.columnCount() == 1,
            "NULL query column count mismatch");

        Check(
            !result.value(0, 0).has_value(),
            "SQL NULL should become std::nullopt");
    }

    void TestRepeatedQuery(
        jobpilot::core::ConnectionPool &pool)
    {
        for (int i = 0; i < 100; ++i)
        {
            const jobpilot::core::Result result =
                pool.Query(
                    "SELECT ? AS value",
                    {static_cast<std::int64_t>(i)});

            Check(
                result.rowCount() == 1,
                "repeated query row count mismatch");

            Check(
                result.columnCount() == 1,
                "repeated query column count mismatch");

            Check(
                result.value(0, 0).has_value(),
                "repeated query returned NULL");

            Check(
                *result.value(0, 0) ==
                    std::to_string(i),
                "repeated query value mismatch");
        }
    }

    void TestConcurrentQuery(
        jobpilot::core::ConnectionPool &pool)
    {
        constexpr int kThreadCount = 8;
        constexpr int kQueriesPerThread = 20;

        std::vector<std::thread> threads;
        threads.reserve(kThreadCount);

        std::mutex errorMutex;
        std::string firstError;

        for (int threadIndex = 0;
             threadIndex < kThreadCount;
             ++threadIndex)
        {
            threads.emplace_back(
                [&pool,
                 &errorMutex,
                 &firstError,
                 threadIndex]()
                {
                    try
                    {
                        for (int i = 0;
                             i < kQueriesPerThread;
                             ++i)
                        {
                            const int value =
                                threadIndex * 1000 + i;

                            const jobpilot::core::Result result =
                                pool.Query(
                                    "SELECT ? AS value",
                                    {static_cast<
                                        std::int64_t>(
                                        value)});

                            Check(
                                result.rowCount() == 1,
                                "concurrent query row count mismatch");

                            Check(
                                result.columnCount() == 1,
                                "concurrent query column count mismatch");

                            Check(
                                result.value(0, 0).has_value(),
                                "concurrent query returned NULL");

                            Check(
                                *result.value(0, 0) ==
                                    std::to_string(value),
                                "concurrent query value mismatch");
                        }
                    }
                    catch (const std::exception &e)
                    {
                        std::lock_guard<std::mutex> lock(
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
            "concurrent query failed: " + firstError);
    }
}

int main()
{
    try
    {
        const char *password =
            std::getenv("MYSQL_PASSWORD");

        if (password == nullptr ||
            *password == '\0')
        {
            std::cerr
                << "MYSQL_PASSWORD is not set"
                << std::endl;

            return 1;
        }

        const std::string host =
            GetEnvOrDefault(
                "MYSQL_HOST",
                "127.0.0.1");

        const std::uint16_t port =
            GetPort();

        const std::string user =
            GetEnvOrDefault(
                "MYSQL_USER",
                "root");

        const std::string database =
            GetEnvOrDefault(
                "MYSQL_DATABASE",
                "jobpilot");

        jobpilot::core::ConnectionPool pool(
            host,
            port,
            user,
            password,
            database,
            8);

        TestBasicQuery(pool);

        std::cout
            << "basic query test passed"
            << std::endl;

        TestNullQuery(pool);

        std::cout
            << "NULL query test passed"
            << std::endl;

        TestRepeatedQuery(pool);

        std::cout
            << "100 repeated queries test passed"
            << std::endl;

        TestConcurrentQuery(pool);

        std::cout
            << "8-thread concurrent query test passed"
            << std::endl;

        std::cout
            << "mysql pool test passed"
            << std::endl;

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr
            << "mysql pool test failed: "
            << e.what()
            << std::endl;

        return 1;
    }
}