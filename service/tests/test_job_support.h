#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>

#include <unistd.h>

#include "jobpilot/database/mysql_pool.h"

namespace jobpilot::test
{
    inline std::string UniqueJobToken(
        const std::string &prefix)
    {
        static std::atomic<std::uint64_t> counter{0};

        return prefix +
               "-" +
               std::to_string(getpid()) +
               "-" +
               std::to_string(
                   counter.fetch_add(
                       1,
                       std::memory_order_relaxed));
    }

    inline std::uint64_t InsertJob(
        jobpilot::core::ConnectionPool *pool,
        const std::string &company,
        const std::string &title,
        const std::optional<std::string> &location,
        const std::optional<std::string> &description,
        const std::string &sourceUrl,
        std::int32_t status,
        const std::string &updateTime)
    {
        if (pool == nullptr)
        {
            throw std::invalid_argument(
                "connection pool must not be null");
        }

        const jobpilot::core::ExecutionResult created =
            pool->Execute(
                "INSERT INTO jobs "
                "(company, title, location, description, "
                "source, source_url, status) "
                "VALUES (?, ?, ?, ?, 'manual', ?, ?)",
                {company,
                 title,
                 location.has_value()
                     ? jobpilot::core::SqlParam(
                           location.value())
                     : jobpilot::core::SqlParam(nullptr),
                 description.has_value()
                     ? jobpilot::core::SqlParam(
                           description.value())
                     : jobpilot::core::SqlParam(nullptr),
                 sourceUrl,
                 static_cast<std::int64_t>(status)});

        if (created.affectedRows != 1 ||
            created.lastInsertId == 0)
        {
            throw std::runtime_error(
                "failed to insert job fixture");
        }

        if (!updateTime.empty())
        {
            pool->Execute(
                "UPDATE jobs SET update_time = ? WHERE id = ?",
                {updateTime, created.lastInsertId});
        }

        return created.lastInsertId;
    }

    inline void InsertJobRequirement(
        jobpilot::core::ConnectionPool *pool,
        std::uint64_t jobId,
        const std::string &skill,
        std::int32_t level)
    {
        if (pool == nullptr)
        {
            throw std::invalid_argument(
                "connection pool must not be null");
        }

        pool->Execute(
            "INSERT INTO job_requirements "
            "(job_id, skill, level) VALUES (?, ?, ?)",
            {jobId,
             skill,
             static_cast<std::int64_t>(level)});
    }

    inline void CleanupJob(
        jobpilot::core::ConnectionPool *pool,
        std::uint64_t jobId)
    {
        if (pool == nullptr || jobId == 0)
        {
            return;
        }

        // 0015A: job_review_items has no FK, so the audit rows are removed with
        // the job row they belong to.
        pool->Execute(
            "DELETE FROM job_review_items WHERE job_id = ?",
            {jobId});

        pool->Execute(
            "DELETE FROM job_requirements WHERE job_id = ?",
            {jobId});

        pool->Execute(
            "DELETE FROM jobs WHERE id = ?",
            {jobId});
    }
} // namespace jobpilot::test
