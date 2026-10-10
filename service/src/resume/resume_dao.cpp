#include "jobpilot/service/resume/resume_dao.h"

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

        jobpilot::core::SqlParam OptionalStringParam(
            const std::optional<std::string> &value)
        {
            if (value.has_value())
            {
                return value.value();
            }

            return nullptr;
        }

        ResumeRecord MakeResumeRecord(
            const jobpilot::core::Result::Row &row)
        {
            if (row.size() != 6)
            {
                throw std::runtime_error(
                    "resume query returned an unexpected column count");
            }

            ResumeRecord resume;
            resume.id =
                ParseUint64(row[0], "resumes.id");
            resume.userId =
                ParseUint64(row[1], "resumes.user_id");
            resume.title =
                RequireValue(row[2], "resumes.title");
            resume.latestVersion =
                ParseInt32(
                    row[3],
                    "resume_versions.latest_version");
            resume.createTime =
                RequireValue(row[4], "resumes.create_time");
            resume.updateTime =
                RequireValue(row[5], "resumes.update_time");

            return resume;
        }
    } // namespace

    ResumeDao::ResumeDao(
        jobpilot::core::ConnectionPool &pool)
        : pool_(pool)
    {
    }

    std::optional<std::uint64_t> ResumeDao::CreateResume(
        jobpilot::core::ConnectionPool::Transaction &transaction,
        std::uint64_t userId,
        const std::string &title)
    {
        const jobpilot::core::ExecutionResult result =
            transaction.Execute(
                "INSERT INTO resumes (user_id, title) "
                "VALUES (?, ?)",
                {userId, title});

        if (result.affectedRows != 1 ||
            result.lastInsertId == 0)
        {
            return std::nullopt;
        }

        return result.lastInsertId;
    }

    bool ResumeDao::LockOwnedResume(
        jobpilot::core::ConnectionPool::Transaction &transaction,
        std::uint64_t userId,
        std::uint64_t resumeId)
    {
        const jobpilot::core::Result result =
            transaction.Query(
                "SELECT id FROM resumes "
                "WHERE id = ? AND user_id = ? "
                "FOR UPDATE",
                {resumeId, userId});

        return result.rowCount() == 1;
    }

    std::int32_t ResumeDao::NextVersion(
        jobpilot::core::ConnectionPool::Transaction &transaction,
        std::uint64_t resumeId)
    {
        const jobpilot::core::Result result =
            transaction.Query(
                "SELECT COALESCE(MAX(version), 0) + 1 "
                "FROM resume_versions WHERE resume_id = ?",
                {resumeId});

        if (result.rowCount() != 1)
        {
            throw std::runtime_error(
                "next version query returned an unexpected row count");
        }

        return ParseInt32(
            result.value(0, 0),
            "resume_versions.next_version");
    }

    std::optional<std::uint64_t> ResumeDao::CreateVersion(
        jobpilot::core::ConnectionPool::Transaction &transaction,
        std::uint64_t resumeId,
        std::int32_t version,
        const std::optional<std::string> &filePath,
        const std::optional<std::string> &content)
    {
        const jobpilot::core::ExecutionResult result =
            transaction.Execute(
                "INSERT INTO resume_versions "
                "(resume_id, version, file_path, content) "
                "VALUES (?, ?, ?, ?)",
                {resumeId,
                 static_cast<std::int64_t>(version),
                 OptionalStringParam(filePath),
                 OptionalStringParam(content)});

        if (result.affectedRows != 1 ||
            result.lastInsertId == 0)
        {
            return std::nullopt;
        }

        return result.lastInsertId;
    }

    std::vector<ResumeRecord> ResumeDao::ListResumes(
        std::uint64_t userId,
        std::size_t limit,
        std::size_t offset)
    {
        const jobpilot::core::Result result =
            pool_.Query(
                "SELECT r.id, r.user_id, r.title, "
                "COALESCE(MAX(v.version), 0) AS latest_version, "
                "r.create_time, "
                "COALESCE(MAX(v.create_time), r.update_time) "
                    "AS updated_at "
                "FROM resumes r "
                "LEFT JOIN resume_versions v "
                    "ON v.resume_id = r.id "
                "WHERE r.user_id = ? "
                "GROUP BY r.id, r.user_id, r.title, "
                    "r.create_time, r.update_time "
                "ORDER BY updated_at DESC, r.id DESC "
                "LIMIT ? OFFSET ?",
                {userId,
                 static_cast<std::uint64_t>(limit),
                 static_cast<std::uint64_t>(offset)});

        std::vector<ResumeRecord> resumes;
        resumes.reserve(result.rowCount());

        for (std::size_t row = 0;
             row < result.rowCount();
             ++row)
        {
            resumes.push_back(
                MakeResumeRecord(result.row(row)));
        }

        return resumes;
    }

    std::size_t ResumeDao::CountResumes(
        std::uint64_t userId)
    {
        const jobpilot::core::Result result =
            pool_.Query(
                "SELECT COUNT(*) FROM resumes "
                "WHERE user_id = ?",
                {userId});

        if (result.rowCount() != 1)
        {
            throw std::runtime_error(
                "resume count query returned an unexpected row count");
        }

        return static_cast<std::size_t>(
            ParseUint64(
                result.value(0, 0),
                "resumes.count"));
    }

    std::optional<ResumeRecord> ResumeDao::FindOwnedResume(
        std::uint64_t userId,
        std::uint64_t resumeId)
    {
        const jobpilot::core::Result result =
            pool_.Query(
                "SELECT r.id, r.user_id, r.title, "
                "COALESCE((SELECT MAX(v.version) "
                    "FROM resume_versions v "
                    "WHERE v.resume_id = r.id), 0), "
                "r.create_time, "
                "COALESCE((SELECT MAX(v.create_time) "
                    "FROM resume_versions v "
                    "WHERE v.resume_id = r.id), r.update_time) "
                "FROM resumes r "
                "WHERE r.id = ? AND r.user_id = ? "
                "LIMIT 1",
                {resumeId, userId});

        if (result.rowCount() == 0)
        {
            return std::nullopt;
        }

        return MakeResumeRecord(result.row(0));
    }

    bool ResumeDao::OwnedVersionExists(
        jobpilot::core::ConnectionPool::Transaction &transaction,
        std::uint64_t userId,
        std::uint64_t resumeVersionId)
    {
        const jobpilot::core::Result result =
            transaction.Query(
                "SELECT rv.id "
                "FROM resume_versions rv "
                "JOIN resumes r ON r.id = rv.resume_id "
                "WHERE rv.id = ? AND r.user_id = ? "
                "LIMIT 1",
                {resumeVersionId, userId});

        return result.rowCount() == 1;
    }

    std::optional<ResumeVersionRecord> ResumeDao::FindOwnedVersion(
        std::uint64_t userId,
        std::uint64_t resumeVersionId)
    {
        const jobpilot::core::Result result =
            pool_.Query(
                "SELECT v.id, v.resume_id, v.version, "
                "v.file_path, v.content, v.create_time "
                "FROM resume_versions v "
                "JOIN resumes r ON r.id = v.resume_id "
                "WHERE v.id = ? AND r.user_id = ? LIMIT 1",
                {resumeVersionId, userId});

        if (result.rowCount() == 0)
        {
            return std::nullopt;
        }

        const auto &values = result.row(0);

        if (values.size() != 6)
        {
            throw std::runtime_error(
                "resume version query returned an unexpected "
                "column count");
        }

        ResumeVersionRecord version;
        version.id =
            ParseUint64(values[0], "resume_versions.id");
        version.resumeId =
            ParseUint64(values[1], "resume_versions.resume_id");
        version.version =
            ParseInt32(values[2], "resume_versions.version");

        if (values[3].has_value())
        {
            version.filePath = values[3].value();
        }

        if (values[4].has_value())
        {
            version.content = values[4].value();
        }

        version.createTime =
            RequireValue(values[5], "resume_versions.create_time");

        return version;
    }

    std::vector<ResumeVersionRecord> ResumeDao::ListVersions(
        std::uint64_t userId,
        std::uint64_t resumeId)
    {
        const jobpilot::core::Result result =
            pool_.Query(
                "SELECT v.id, v.resume_id, v.version, "
                "v.file_path, v.content, v.create_time "
                "FROM resume_versions v "
                "JOIN resumes r ON r.id = v.resume_id "
                "WHERE r.user_id = ? AND v.resume_id = ? "
                "ORDER BY v.version ASC",
                {userId, resumeId});

        std::vector<ResumeVersionRecord> versions;
        versions.reserve(result.rowCount());

        for (std::size_t row = 0;
             row < result.rowCount();
             ++row)
        {
            const auto &values = result.row(row);

            if (values.size() != 6)
            {
                throw std::runtime_error(
                    "resume version query returned an unexpected "
                    "column count");
            }

            ResumeVersionRecord version;
            version.id =
                ParseUint64(values[0], "resume_versions.id");
            version.resumeId =
                ParseUint64(
                    values[1],
                    "resume_versions.resume_id");
            version.version =
                ParseInt32(
                    values[2],
                    "resume_versions.version");

            if (values[3].has_value())
            {
                version.filePath = values[3].value();
            }

            if (values[4].has_value())
            {
                version.content = values[4].value();
            }

            version.createTime =
                RequireValue(
                    values[5],
                    "resume_versions.create_time");

            versions.push_back(std::move(version));
        }

        return versions;
    }

} // namespace jobpilot::service
