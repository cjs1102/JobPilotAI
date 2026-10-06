#include "jobpilot/database/mysql_pool.h"

#include <cstdlib>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace
{
    std::string GetEnvironment(
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
        const std::string value =
            GetEnvironment("MYSQL_PORT", "3306");

        const unsigned long port =
            std::stoul(value);

        if (port == 0 || port > 65535)
        {
            throw std::invalid_argument(
                "MYSQL_PORT must be between 1 and 65535");
        }

        return static_cast<std::uint16_t>(port);
    }

    std::string RequireValue(
        const jobpilot::core::Result::Value &value)
    {
        if (!value.has_value())
        {
            throw std::runtime_error("expected non-null query value");
        }

        return value.value();
    }

    jobpilot::core::ConnectionPool MakePool()
    {
        const char *password = std::getenv("MYSQL_PASSWORD");

        if (password == nullptr || *password == '\0')
        {
            throw std::runtime_error(
                "MYSQL_PASSWORD is not set");
        }

        return jobpilot::core::ConnectionPool(
            GetEnvironment("MYSQL_HOST", "127.0.0.1"),
            GetPort(),
            GetEnvironment("MYSQL_USER", "root"),
            password,
            GetEnvironment("MYSQL_DATABASE", "jobpilot"),
            4);
    }

    class SchemaTest : public ::testing::Test
    {
    protected:
        SchemaTest()
            : pool(MakePool()),
              database(
                  GetEnvironment(
                      "MYSQL_DATABASE",
                      "jobpilot"))
        {
        }

        jobpilot::core::ConnectionPool pool;
        std::string database;
    };

    TEST_F(SchemaTest, DatabaseUsesExpectedCharsetAndCollation)
    {
        const jobpilot::core::Result result =
            pool.Query(
                "SELECT default_character_set_name, "
                "default_collation_name "
                "FROM information_schema.schemata "
                "WHERE schema_name = ?",
                {database});

        ASSERT_EQ(result.rowCount(), 1U);
        EXPECT_EQ(RequireValue(result.value(0, 0)), "utf8mb4");
        EXPECT_EQ(
            RequireValue(result.value(0, 1)),
            "utf8mb4_general_ci");
    }

    TEST_F(SchemaTest, CreatesAllExpectedTables)
    {
        const jobpilot::core::Result result =
            pool.Query(
                "SELECT table_name, engine, table_collation "
                "FROM information_schema.tables "
                "WHERE table_schema = ? AND table_type = 'BASE TABLE'",
                {database});

        std::set<std::string> tables;

        for (std::size_t row = 0; row < result.rowCount(); ++row)
        {
            const std::string table =
                RequireValue(result.value(row, 0));

            tables.insert(table);

            EXPECT_EQ(
                RequireValue(result.value(row, 1)),
                "InnoDB");
            EXPECT_EQ(
                RequireValue(result.value(row, 2)),
                "utf8mb4_general_ci");
        }

        const std::set<std::string> expected = {
            "users",
            "profiles",
            "skills",
            "resumes",
            "resume_versions",
            "jobs",
            "job_requirements",
            "applications",
            "ai_tasks",
            "interview_records",
            "notifications"};

        EXPECT_EQ(tables, expected);
    }

    TEST_F(SchemaTest, CreatesRequiredColumns)
    {
        const jobpilot::core::Result result =
            pool.Query(
                "SELECT table_name, column_name "
                "FROM information_schema.columns "
                "WHERE table_schema = ?",
                {database});

        std::map<std::string, std::set<std::string>> columns;

        for (std::size_t row = 0;
             row < result.rowCount();
             ++row)
        {
            columns[RequireValue(result.value(row, 0))]
                .insert(RequireValue(result.value(row, 1)));
        }

        EXPECT_NE(
            columns["users"].find("password_hash"),
            columns["users"].end());
        EXPECT_NE(
            columns["profiles"].find("target_position"),
            columns["profiles"].end());
        EXPECT_NE(
            columns["resume_versions"].find("content"),
            columns["resume_versions"].end());
        EXPECT_NE(
            columns["jobs"].find("source_url"),
            columns["jobs"].end());
        EXPECT_NE(
            columns["applications"].find("resume_version_id"),
            columns["applications"].end());
        EXPECT_NE(
            columns["ai_tasks"].find("retry_count"),
            columns["ai_tasks"].end());
        EXPECT_NE(
            columns["notifications"].find("status"),
            columns["notifications"].end());
    }

    TEST_F(SchemaTest, CreatesRequiredUniqueIndexes)
    {
        const jobpilot::core::Result result =
            pool.Query(
                "SELECT table_name, index_name "
                "FROM information_schema.statistics "
                "WHERE table_schema = ? AND non_unique = 0 "
                "AND index_name <> 'PRIMARY'",
                {database});

        std::map<std::string, std::set<std::string>> indexes;

        for (std::size_t row = 0;
             row < result.rowCount();
             ++row)
        {
            indexes[RequireValue(result.value(row, 0))]
                .insert(RequireValue(result.value(row, 1)));
        }

        EXPECT_NE(
            indexes["users"].find("uk_email"),
            indexes["users"].end());
        EXPECT_NE(
            indexes["profiles"].find("uk_user_id"),
            indexes["profiles"].end());
        EXPECT_NE(
            indexes["skills"].find("uk_user_skill"),
            indexes["skills"].end());
        EXPECT_NE(
            indexes["resume_versions"].find("uk_resume_version"),
            indexes["resume_versions"].end());
        EXPECT_NE(
            indexes["jobs"].find("uk_source_url"),
            indexes["jobs"].end());
        EXPECT_NE(
            indexes["job_requirements"].find("uk_job_skill"),
            indexes["job_requirements"].end());
        EXPECT_NE(
            indexes["applications"].find("uk_user_job"),
            indexes["applications"].end());
    }
}
