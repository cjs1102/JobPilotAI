#include "jobpilot/service/application/application_dao.h"

#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "jobpilot/database/mysql_pool.h"

namespace jobpilot::service
{
    namespace
    {
        struct ApplicationFilter
        {
            std::string sql;
            std::vector<jobpilot::core::SqlParam> params;
        };

        std::string RequireValue(
            const jobpilot::core::Result::Value &value,
            const std::string &field)
        {
            if (!value.has_value())
            {
                throw std::runtime_error(
                    field + " must not be NULL");
            }

            return value.value();
        }

        std::uint64_t ParseUint64(
            const jobpilot::core::Result::Value &value,
            const std::string &field)
        {
            const std::string text =
                RequireValue(value, field);

            std::size_t parsed = 0;
            const unsigned long long result =
                std::stoull(text, &parsed);

            if (parsed != text.size())
            {
                throw std::runtime_error(
                    field + " contains invalid characters");
            }

            return static_cast<std::uint64_t>(
                result);
        }

        std::int32_t ParseInt32(
            const jobpilot::core::Result::Value &value,
            const std::string &field)
        {
            const std::string text =
                RequireValue(value, field);

            std::size_t parsed = 0;
            const long result =
                std::stol(text, &parsed);

            if (parsed != text.size() ||
                result <
                    std::numeric_limits<
                        std::int32_t>::min() ||
                result >
                    std::numeric_limits<
                        std::int32_t>::max())
            {
                throw std::runtime_error(
                    field + " contains an invalid value");
            }

            return static_cast<std::int32_t>(
                result);
        }

        ApplicationFilter BuildListFilter(
            std::uint64_t userId,
            const std::optional<std::int32_t> &status)
        {
            ApplicationFilter filter;
            filter.sql = " WHERE a.user_id = ?";
            filter.params.push_back(userId);

            if (status.has_value())
            {
                filter.sql += " AND a.status = ?";
                filter.params.push_back(
                    static_cast<std::int64_t>(
                        status.value()));
            }

            return filter;
        }

        ApplicationRecord MakeApplicationRecord(
            const jobpilot::core::Result::Row &row)
        {
            if (row.size() != 9)
            {
                throw std::runtime_error(
                    "application query returned an unexpected "
                    "column count");
            }

            ApplicationRecord application;
            application.id =
                ParseUint64(row[0], "applications.id");
            application.userId =
                ParseUint64(row[1], "applications.user_id");
            application.jobId =
                ParseUint64(row[2], "applications.job_id");
            application.resumeVersionId =
                ParseUint64(
                    row[3],
                    "applications.resume_version_id");
            application.status =
                ParseInt32(row[4], "applications.status");

            if (row[5].has_value())
            {
                application.applyTime = row[5].value();
            }

            if (row[6].has_value())
            {
                application.remark = row[6].value();
            }

            application.createTime =
                RequireValue(
                    row[7],
                    "applications.create_time");
            application.updateTime =
                RequireValue(
                    row[8],
                    "applications.update_time");

            return application;
        }
    } // namespace

    ApplicationDao::ApplicationDao(
        jobpilot::core::ConnectionPool &pool)
        : pool_(pool)
    {
    }

    ApplicationCreateResult ApplicationDao::Create(
        jobpilot::core::ConnectionPool::Transaction &transaction,
        std::uint64_t userId,
        std::uint64_t jobId,
        std::uint64_t resumeVersionId)
    {
        const jobpilot::core::ExecutionResult result =
            transaction.Execute(
                "INSERT INTO applications "
                "(user_id, job_id, resume_version_id, status) "
                "VALUES (?, ?, ?, 0) "
                "ON DUPLICATE KEY UPDATE "
                "id = LAST_INSERT_ID(id)",
                {userId, jobId, resumeVersionId});

        if (result.lastInsertId == 0)
        {
            throw std::runtime_error(
                "application insert returned no id");
        }

        if (result.affectedRows == 1)
        {
            return ApplicationCreateResult{
                ApplicationCreateStatus::kCreated,
                result.lastInsertId};
        }

        if (result.affectedRows == 0 ||
            result.affectedRows == 2)
        {
            return ApplicationCreateResult{
                ApplicationCreateStatus::kDuplicate,
                result.lastInsertId};
        }

        throw std::runtime_error(
            "application insert returned unexpected affected rows");
    }

    std::optional<ApplicationRecord>
    ApplicationDao::FindOwnedForUpdate(
        jobpilot::core::ConnectionPool::Transaction &transaction,
        std::uint64_t userId,
        std::uint64_t applicationId)
    {
        const jobpilot::core::Result result =
            transaction.Query(
                "SELECT id, user_id, job_id, resume_version_id, "
                "status, apply_time, remark, create_time, update_time "
                "FROM applications "
                "WHERE id = ? AND user_id = ? "
                "FOR UPDATE",
                {applicationId, userId});

        if (result.rowCount() == 0)
        {
            return std::nullopt;
        }

        return MakeApplicationRecord(result.row(0));
    }

    std::vector<ApplicationListItem> ApplicationDao::List(
        std::uint64_t userId,
        const std::optional<std::int32_t> &status,
        std::size_t limit,
        std::size_t offset)
    {
        const ApplicationFilter filter =
            BuildListFilter(userId, status);

        std::vector<jobpilot::core::SqlParam> params =
            filter.params;
        params.push_back(
            static_cast<std::uint64_t>(limit));
        params.push_back(
            static_cast<std::uint64_t>(offset));

        const jobpilot::core::Result result =
            pool_.Query(
                "SELECT a.id, a.job_id, j.title, j.company, "
                "a.status, a.apply_time, a.remark "
                "FROM applications a "
                "JOIN jobs j ON j.id = a.job_id" +
                    filter.sql +
                    " ORDER BY a.update_time DESC, a.id DESC "
                    "LIMIT ? OFFSET ?",
                params);

        std::vector<ApplicationListItem> applications;
        applications.reserve(result.rowCount());

        for (std::size_t row = 0;
             row < result.rowCount();
             ++row)
        {
            const auto &values = result.row(row);

            if (values.size() != 7)
            {
                throw std::runtime_error(
                    "application list returned an unexpected "
                    "column count");
            }

            ApplicationListItem item;
            item.id =
                ParseUint64(values[0], "applications.id");
            item.jobId =
                ParseUint64(values[1], "applications.job_id");
            item.jobTitle =
                RequireValue(values[2], "jobs.title");
            item.company =
                RequireValue(values[3], "jobs.company");
            item.status =
                ParseInt32(values[4], "applications.status");

            if (values[5].has_value())
            {
                item.applyTime = values[5].value();
            }

            if (values[6].has_value())
            {
                item.remark = values[6].value();
            }

            applications.push_back(std::move(item));
        }

        return applications;
    }

    std::size_t ApplicationDao::Count(
        std::uint64_t userId,
        const std::optional<std::int32_t> &status)
    {
        const ApplicationFilter filter =
            BuildListFilter(userId, status);

        const jobpilot::core::Result result =
            pool_.Query(
                "SELECT COUNT(*) "
                "FROM applications a" +
                    filter.sql,
                filter.params);

        if (result.rowCount() != 1)
        {
            throw std::runtime_error(
                "application count returned an unexpected row count");
        }

        return static_cast<std::size_t>(
            ParseUint64(
                result.value(0, 0),
                "applications.count"));
    }

    bool ApplicationDao::UpdateStatus(
        jobpilot::core::ConnectionPool::Transaction &transaction,
        std::uint64_t userId,
        std::uint64_t applicationId,
        std::int32_t status,
        bool setApplyTime)
    {
        jobpilot::core::ExecutionResult result;

        if (setApplyTime)
        {
            result = transaction.Execute(
                "UPDATE applications "
                "SET status = ?, "
                "apply_time = COALESCE(apply_time, CURRENT_TIMESTAMP) "
                "WHERE id = ? AND user_id = ?",
                {static_cast<std::int64_t>(status),
                 applicationId,
                 userId});
        }
        else
        {
            result = transaction.Execute(
                "UPDATE applications "
                "SET status = ? "
                "WHERE id = ? AND user_id = ?",
                {static_cast<std::int64_t>(status),
                 applicationId,
                 userId});
        }

        return result.affectedRows == 1;
    }

    ApplicationStatistics ApplicationDao::Statistics(
        std::uint64_t userId)
    {
        const jobpilot::core::Result result =
            pool_.Query(
                "SELECT COUNT(*), "
                "COALESCE(SUM(CASE WHEN status IN (3, 4, 5) "
                    "THEN 1 ELSE 0 END), 0), "
                "COALESCE(SUM(CASE WHEN status = 6 "
                    "THEN 1 ELSE 0 END), 0), "
                "COALESCE(SUM(CASE WHEN status = 7 "
                    "THEN 1 ELSE 0 END), 0) "
                "FROM applications "
                "WHERE user_id = ?",
                {userId});

        if (result.rowCount() != 1 ||
            result.columnCount() != 4)
        {
            throw std::runtime_error(
                "application statistics returned an unexpected shape");
        }

        ApplicationStatistics statistics;
        statistics.total = static_cast<std::size_t>(
            ParseUint64(
                result.value(0, 0),
                "applications.total"));
        statistics.interview = static_cast<std::size_t>(
            ParseUint64(
                result.value(0, 1),
                "applications.interview"));
        statistics.offer = static_cast<std::size_t>(
            ParseUint64(
                result.value(0, 2),
                "applications.offer"));
        statistics.rejected = static_cast<std::size_t>(
            ParseUint64(
                result.value(0, 3),
                "applications.rejected"));

        return statistics;
    }

} // namespace jobpilot::service
