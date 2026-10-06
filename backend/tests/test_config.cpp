#include "jobpilot/core/config.h"

#include <cstdio>
#include <fstream>
#include <string>

#include <gtest/gtest.h>
#include <unistd.h>

namespace jobpilot::core
{
    namespace
    {
        Environment BaseEnvironment()
        {
            return {
                {"MYSQL_PASSWORD", "secret"}};
        }
    } // namespace

    TEST(ConfigTest, LoadsDefaults)
    {
        const Config config =
            Config::LoadFromString("", BaseEnvironment());

        EXPECT_EQ(config.server.host, "0.0.0.0");
        EXPECT_EQ(config.server.port, 8080);
        EXPECT_EQ(config.mysql.host, "localhost");
        EXPECT_EQ(config.mysql.port, 3306);
        EXPECT_EQ(config.mysql.database, "jobpilot");
        EXPECT_EQ(config.mysql.user, "jobpilot");
        EXPECT_EQ(config.mysql.maxConn, 10U);
        EXPECT_EQ(config.redis.host, "localhost");
        EXPECT_EQ(config.redis.port, 6379);
        EXPECT_EQ(config.redis.maxConn, 4U);
        EXPECT_EQ(config.redis.timeoutMs, 1000);
        EXPECT_EQ(config.log.level, "info");
    }

    TEST(ConfigTest, LoadsYaml)
    {
        const Config config =
            Config::LoadFromString(
                "server:\n"
                "  host: 127.0.0.1\n"
                "  port: 9090\n"
                "mysql:\n"
                "  host: db\n"
                "  port: 3307\n"
                "  database: testdb\n"
                "  user: tester\n"
                "  max_conn: 20\n"
                "redis:\n"
                "  host: cache\n"
                "  port: 6380\n"
                "  max_conn: 6\n"
                "  timeout_ms: 2000\n"
                "log:\n"
                "  level: debug\n",
                BaseEnvironment());

        EXPECT_EQ(config.server.host, "127.0.0.1");
        EXPECT_EQ(config.server.port, 9090);
        EXPECT_EQ(config.mysql.host, "db");
        EXPECT_EQ(config.mysql.port, 3307);
        EXPECT_EQ(config.mysql.database, "testdb");
        EXPECT_EQ(config.mysql.user, "tester");
        EXPECT_EQ(config.mysql.maxConn, 20U);
        EXPECT_EQ(config.redis.host, "cache");
        EXPECT_EQ(config.redis.port, 6380);
        EXPECT_EQ(config.redis.maxConn, 6U);
        EXPECT_EQ(config.redis.timeoutMs, 2000);
        EXPECT_EQ(config.log.level, "debug");
    }

    TEST(ConfigTest, EnvironmentOverridesYaml)
    {
        Environment environment = {
            {"JOBPILOT_HOST", "0.0.0.0"},
            {"JOBPILOT_PORT", "8081"},
            {"MYSQL_HOST", "mysql-env"},
            {"MYSQL_PORT", "33306"},
            {"MYSQL_DATABASE", "envdb"},
            {"MYSQL_USER", "envuser"},
            {"MYSQL_PASSWORD", "env-secret"},
            {"MYSQL_MAX_CONN", "30"},
            {"REDIS_HOST", "redis-env"},
            {"REDIS_PORT", "36379"},
            {"REDIS_MAX_CONN", "8"},
            {"REDIS_TIMEOUT_MS", "3000"},
            {"LOG_LEVEL", "warn"}};

        const Config config =
            Config::LoadFromString(
                "server:\n"
                "  host: yaml-host\n"
                "  port: 1111\n"
                "mysql:\n"
                "  host: yaml-mysql\n"
                "  port: 2222\n"
                "  database: yaml-db\n"
                "  user: yaml-user\n"
                "  max_conn: 2\n"
                "redis:\n"
                "  host: yaml-redis\n"
                "  port: 3333\n"
                "  max_conn: 2\n"
                "  timeout_ms: 100\n"
                "log:\n"
                "  level: error\n",
                environment);

        EXPECT_EQ(config.server.host, "0.0.0.0");
        EXPECT_EQ(config.server.port, 8081);
        EXPECT_EQ(config.mysql.host, "mysql-env");
        EXPECT_EQ(config.mysql.port, 33306);
        EXPECT_EQ(config.mysql.database, "envdb");
        EXPECT_EQ(config.mysql.user, "envuser");
        EXPECT_EQ(config.mysql.password, "env-secret");
        EXPECT_EQ(config.mysql.maxConn, 30U);
        EXPECT_EQ(config.redis.host, "redis-env");
        EXPECT_EQ(config.redis.port, 36379);
        EXPECT_EQ(config.redis.maxConn, 8U);
        EXPECT_EQ(config.redis.timeoutMs, 3000);
        EXPECT_EQ(config.log.level, "warn");
    }

    TEST(ConfigTest, RequiresMySqlPassword)
    {
        EXPECT_THROW(
            Config::LoadFromString("", {}),
            ConfigError);
    }

    TEST(ConfigTest, RejectsInvalidPort)
    {
        EXPECT_THROW(
            Config::LoadFromString(
                "server:\n"
                "  port: 70000\n",
                BaseEnvironment()),
            ConfigError);
    }

    TEST(ConfigTest, RejectsTypeErrors)
    {
        EXPECT_THROW(
            Config::LoadFromString(
                "server:\n"
                "  port: invalid\n",
                BaseEnvironment()),
            ConfigError);
    }

    TEST(ConfigTest, RejectsPasswordInYaml)
    {
        EXPECT_THROW(
            Config::LoadFromString(
                "mysql:\n"
                "  password: not-allowed\n",
                BaseEnvironment()),
            ConfigError);
    }

    TEST(ConfigTest, ReportsMissingFile)
    {
        EXPECT_THROW(
            Config::LoadFromFile(
                "/tmp/jobpilot-config-does-not-exist.yaml",
                BaseEnvironment()),
            ConfigError);
    }

    TEST(ConfigTest, LoadsFile)
    {
        const std::string path =
            "/tmp/jobpilot-config-test-" +
            std::to_string(getpid()) +
            ".yaml";

        {
            std::ofstream output(path);
            ASSERT_TRUE(output);
            output << "server:\n"
                      "  port: 9091\n";
        }

        const Config config =
            Config::LoadFromFile(
                path,
                BaseEnvironment());

        EXPECT_EQ(config.server.port, 9091);

        std::remove(path.c_str());
    }

    TEST(ConfigTest, RejectsMalformedYaml)
    {
        EXPECT_THROW(
            Config::LoadFromString(
                "server: [\n",
                BaseEnvironment()),
            ConfigError);
    }

} // namespace jobpilot::core
