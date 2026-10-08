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

    std::string OptionalValue(
        const jobpilot::core::Result::Value &value)
    {
        if (!value.has_value())
        {
            return std::string();
        }

        return value.value();
    }

    struct ColumnContract
    {
        std::string nullable;
        std::string defaultValue;
    };

    std::map<std::string, ColumnContract> LoadJobColumns(
        jobpilot::core::ConnectionPool &pool,
        const std::string &database)
    {
        const jobpilot::core::Result result =
            pool.Query(
                "SELECT column_name, is_nullable, column_default "
                "FROM information_schema.columns "
                "WHERE table_schema = ? AND table_name = 'jobs'",
                {database});

        std::map<std::string, ColumnContract> columns;

        for (std::size_t row = 0; row < result.rowCount(); ++row)
        {
            columns[RequireValue(result.value(row, 0))] =
                ColumnContract{
                    RequireValue(result.value(row, 1)),
                    OptionalValue(result.value(row, 2))};
        }

        return columns;
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

    TEST_F(SchemaTest, CreatesFreshnessObservationColumns)
    {
        const std::map<std::string, ColumnContract> columns =
            LoadJobColumns(pool, database);

        const std::vector<std::string> optionalColumns = {
            "source_scope",
            "external_id",
            "published_at",
            "last_seen_at",
            "last_verified_at",
            "closed_at"};

        for (const std::string &name : optionalColumns)
        {
            const auto found = columns.find(name);

            ASSERT_NE(found, columns.end()) << name;
            EXPECT_EQ(found->second.nullable, "YES") << name;
        }

        const auto missingCount = columns.find("missing_count");

        ASSERT_NE(missingCount, columns.end());
        EXPECT_EQ(missingCount->second.nullable, "NO");
        EXPECT_EQ(missingCount->second.defaultValue, "0");

        const auto sourceStatus = columns.find("source_status");

        ASSERT_NE(sourceStatus, columns.end());
        EXPECT_EQ(sourceStatus->second.nullable, "NO");
        EXPECT_EQ(sourceStatus->second.defaultValue, "unknown");
    }

    TEST_F(SchemaTest, CreatesSourceScopeStatusIndex)
    {
        const jobpilot::core::Result result =
            pool.Query(
                "SELECT non_unique, seq_in_index, column_name "
                "FROM information_schema.statistics "
                "WHERE table_schema = ? AND table_name = 'jobs' "
                "AND index_name = 'idx_source_scope_status' "
                "ORDER BY seq_in_index",
                {database});

        ASSERT_EQ(result.rowCount(), 3U);

        EXPECT_EQ(RequireValue(result.value(0, 0)), "1");
        EXPECT_EQ(RequireValue(result.value(0, 1)), "1");
        EXPECT_EQ(RequireValue(result.value(0, 2)), "source");
        EXPECT_EQ(RequireValue(result.value(1, 1)), "2");
        EXPECT_EQ(RequireValue(result.value(1, 2)), "source_scope");
        EXPECT_EQ(RequireValue(result.value(2, 1)), "3");
        EXPECT_EQ(RequireValue(result.value(2, 2)), "status");
    }

    TEST_F(SchemaTest, SourceUrlUniquenessIsUnchanged)
    {
        const jobpilot::core::Result result =
            pool.Query(
                "SELECT non_unique, column_name "
                "FROM information_schema.statistics "
                "WHERE table_schema = ? AND table_name = 'jobs' "
                "AND index_name = 'uk_source_url' "
                "ORDER BY seq_in_index",
                {database});

        ASSERT_EQ(result.rowCount(), 1U);
        EXPECT_EQ(RequireValue(result.value(0, 0)), "0");
        EXPECT_EQ(RequireValue(result.value(0, 1)), "source_url");
    }

    TEST_F(SchemaTest, LegacyJobRowReadsWithFreshnessDefaults)
    {
        const std::string url =
            "https://schema-test.invalid/jobs/0012-freshness-defaults";

        pool.Execute(
            "DELETE FROM jobs WHERE source_url = ?",
            {url});

        pool.Execute(
            "INSERT INTO jobs (company, title, source, source_url) "
            "VALUES (?, ?, ?, ?)",
            {
                std::string("schema-test"),
                std::string("freshness defaults"),
                std::string("manual"),
                url,
            });

        const jobpilot::core::Result result =
            pool.Query(
                "SELECT status, missing_count, source_status, "
                "source_scope, last_seen_at, closed_at "
                "FROM jobs WHERE source_url = ?",
                {url});

        ASSERT_EQ(result.rowCount(), 1U);
        EXPECT_EQ(RequireValue(result.value(0, 0)), "1");
        EXPECT_EQ(RequireValue(result.value(0, 1)), "0");
        EXPECT_EQ(RequireValue(result.value(0, 2)), "unknown");
        EXPECT_FALSE(result.value(0, 3).has_value());
        EXPECT_FALSE(result.value(0, 4).has_value());
        EXPECT_FALSE(result.value(0, 5).has_value());

        const jobpilot::core::ExecutionResult cleanup =
            pool.Execute(
                "DELETE FROM jobs WHERE source_url = ?",
                {url});

        EXPECT_EQ(cleanup.affectedRows, 1U);
    }

    TEST_F(SchemaTest, CreatesAiTaskLeaseColumnsAndIndex)
    {
        const jobpilot::core::Result columns =
            pool.Query(
                "SELECT column_name, is_nullable, column_default "
                "FROM information_schema.columns "
                "WHERE table_schema = ? AND table_name = 'ai_tasks'",
                {database});

        std::map<std::string, ColumnContract> contract;

        for (std::size_t row = 0;
             row < columns.rowCount();
             ++row)
        {
            contract[RequireValue(columns.value(row, 0))] =
                ColumnContract{
                    RequireValue(columns.value(row, 1)),
                    OptionalValue(columns.value(row, 2))};
        }

        for (const char *name :
             {"locked_by", "lease_expire_at", "rule_result"})
        {
            const auto found = contract.find(name);

            ASSERT_NE(found, contract.end()) << name;
            EXPECT_EQ(found->second.nullable, "YES") << name;
        }

        const auto attempts = contract.find("retry_count");

        ASSERT_NE(attempts, contract.end());
        EXPECT_EQ(attempts->second.nullable, "NO");
        EXPECT_EQ(attempts->second.defaultValue, "0");

        const jobpilot::core::Result index =
            pool.Query(
                "SELECT non_unique, seq_in_index, column_name "
                "FROM information_schema.statistics "
                "WHERE table_schema = ? AND table_name = 'ai_tasks' "
                "AND index_name = 'idx_status_lease' "
                "ORDER BY seq_in_index",
                {database});

        ASSERT_EQ(index.rowCount(), 2U);
        EXPECT_EQ(RequireValue(index.value(0, 0)), "1");
        EXPECT_EQ(RequireValue(index.value(0, 2)), "status");
        EXPECT_EQ(RequireValue(index.value(1, 2)), "lease_expire_at");

        const jobpilot::core::Result existing =
            pool.Query(
                "SELECT DISTINCT index_name "
                "FROM information_schema.statistics "
                "WHERE table_schema = ? AND table_name = 'ai_tasks'",
                {database});

        std::set<std::string> names;

        for (std::size_t row = 0;
             row < existing.rowCount();
             ++row)
        {
            names.insert(
                RequireValue(existing.value(row, 0)));
        }

        EXPECT_NE(names.find("idx_user_status"), names.end());
        EXPECT_NE(names.find("idx_status"), names.end());
        EXPECT_NE(names.find("idx_type_status"), names.end());
    }
}
