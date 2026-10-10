#include "jobpilot/service/resume/resume_artifact_dao.h"

#include <stdexcept>
#include <string>
#include <utility>

#include "jobpilot/database/mysql_pool.h"

namespace jobpilot::service
{
    namespace
    {
        constexpr const char *kArtifactColumns =
            "id, user_id, job_id, application_id, base_version_id, "
            "artifact_type, language, content, content_bytes, generator, "
            "source_version, fact_check_status, fact_check_report, "
            "confirmed_at, create_time, update_time";

        std::string RequireValue(
            const std::optional<std::string> &value,
            const char *field)
        {
            if (!value.has_value())
            {
                throw std::runtime_error(
                    std::string("missing column ") + field);
            }

            return value.value();
        }

        std::uint64_t ParseUint64(
            const std::optional<std::string> &value,
            const char *field)
        {
            const std::string text = RequireValue(value, field);

            try
            {
                return static_cast<std::uint64_t>(
                    std::stoull(text));
            }
            catch (const std::exception &)
            {
                throw std::runtime_error(
                    std::string("invalid column ") + field);
            }
        }

        std::int32_t ParseInt32(
            const std::optional<std::string> &value,
            const char *field)
        {
            const std::string text = RequireValue(value, field);

            try
            {
                return static_cast<std::int32_t>(
                    std::stol(text));
            }
            catch (const std::exception &)
            {
                throw std::runtime_error(
                    std::string("invalid column ") + field);
            }
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

        jobpilot::core::SqlParam OptionalUint64Param(
            const std::optional<std::uint64_t> &value)
        {
            if (value.has_value())
            {
                return value.value();
            }

            return nullptr;
        }

        ResumeArtifactRecord MakeRecord(
            const jobpilot::core::Result::Row &row)
        {
            if (row.size() != 16)
            {
                throw std::runtime_error(
                    "artifact query returned an unexpected column count");
            }

            ResumeArtifactRecord record;
            record.id =
                ParseUint64(row[0], "resume_artifacts.id");
            record.userId =
                ParseUint64(row[1], "resume_artifacts.user_id");

            if (row[2].has_value())
            {
                record.jobId =
                    ParseUint64(row[2], "resume_artifacts.job_id");
            }

            if (row[3].has_value())
            {
                record.applicationId = ParseUint64(
                    row[3],
                    "resume_artifacts.application_id");
            }

            record.baseVersionId = ParseUint64(
                row[4],
                "resume_artifacts.base_version_id");
            record.artifactType = RequireValue(
                row[5],
                "resume_artifacts.artifact_type");
            record.language =
                RequireValue(row[6], "resume_artifacts.language");

            if (row[7].has_value())
            {
                record.content = row[7].value();
            }

            if (row[8].has_value())
            {
                record.contentBytes = static_cast<std::uint32_t>(
                    ParseUint64(
                        row[8],
                        "resume_artifacts.content_bytes"));
            }

            record.generator =
                RequireValue(row[9], "resume_artifacts.generator");

            if (row[10].has_value())
            {
                record.sourceVersion = row[10].value();
            }

            record.factCheckStatus = ParseInt32(
                row[11],
                "resume_artifacts.fact_check_status");

            if (row[12].has_value())
            {
                record.factCheckReport = row[12].value();
            }

            if (row[13].has_value())
            {
                record.confirmedAt = row[13].value();
            }

            record.createTime = RequireValue(
                row[14],
                "resume_artifacts.create_time");
            record.updateTime = RequireValue(
                row[15],
                "resume_artifacts.update_time");

            return record;
        }
    } // namespace

    ResumeArtifactDao::ResumeArtifactDao(
        jobpilot::core::ConnectionPool &pool,
        IResumeDao &resumeDao)
        : pool_(pool),
          resumeDao_(resumeDao)
    {
    }

    ArtifactOwnership ResumeArtifactDao::CheckOwnership(
        jobpilot::core::ConnectionPool::Transaction &transaction,
        std::uint64_t userId,
        std::uint64_t baseVersionId,
        const std::optional<std::uint64_t> &applicationId)
    {
        if (userId == 0 ||
            !resumeDao_.OwnedVersionExists(
                transaction,
                userId,
                baseVersionId))
        {
            return ArtifactOwnership::kBaseVersionNotFound;
        }

        if (applicationId.has_value())
        {
            const jobpilot::core::Result result =
                transaction.Query(
                    "SELECT id FROM applications "
                    "WHERE id = ? AND user_id = ? LIMIT 1",
                    {applicationId.value(), userId});

            if (result.rowCount() != 1)
            {
                return ArtifactOwnership::kApplicationNotFound;
            }
        }

        return ArtifactOwnership::kOk;
    }

    std::optional<std::uint64_t> ResumeArtifactDao::Create(
        jobpilot::core::ConnectionPool::Transaction &transaction,
        const ResumeArtifactWrite &write)
    {
        const bool hasContent = write.content.has_value();
        const std::uint64_t contentBytes =
            hasContent
                ? static_cast<std::uint64_t>(
                      write.content.value().size())
                : 0;

        const jobpilot::core::ExecutionResult result =
            transaction.Execute(
                "INSERT INTO resume_artifacts "
                "(user_id, job_id, application_id, base_version_id, "
                "artifact_type, language, content, content_bytes, "
                "generator, source_version, fact_check_status, "
                "fact_check_report) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                {write.userId,
                 OptionalUint64Param(write.jobId),
                 OptionalUint64Param(write.applicationId),
                 write.baseVersionId,
                 write.artifactType,
                 write.language,
                 OptionalStringParam(write.content),
                 hasContent
                     ? jobpilot::core::SqlParam{contentBytes}
                     : jobpilot::core::SqlParam{nullptr},
                 write.generator,
                 OptionalStringParam(write.sourceVersion),
                 static_cast<std::int64_t>(write.factCheckStatus),
                 OptionalStringParam(write.factCheckReport)});

        if (result.affectedRows != 1 ||
            result.lastInsertId == 0)
        {
            return std::nullopt;
        }

        return result.lastInsertId;
    }

    std::optional<ResumeArtifactRecord> ResumeArtifactDao::Find(
        std::uint64_t userId,
        std::uint64_t artifactId)
    {
        const jobpilot::core::Result result =
            pool_.Query(
                std::string("SELECT ") + kArtifactColumns +
                    " FROM resume_artifacts "
                    "WHERE id = ? AND user_id = ? LIMIT 1",
                {artifactId, userId});

        if (result.rowCount() == 0)
        {
            return std::nullopt;
        }

        return MakeRecord(result.row(0));
    }

    std::vector<ResumeArtifactRecord> ResumeArtifactDao::List(
        std::uint64_t userId)
    {
        const jobpilot::core::Result result =
            pool_.Query(
                std::string("SELECT ") + kArtifactColumns +
                    " FROM resume_artifacts WHERE user_id = ? "
                    "ORDER BY id ASC",
                {userId});

        std::vector<ResumeArtifactRecord> artifacts;
        artifacts.reserve(result.rowCount());

        for (std::size_t index = 0;
             index < result.rowCount();
             ++index)
        {
            artifacts.push_back(
                MakeRecord(result.row(index)));
        }

        return artifacts;
    }

    bool ResumeArtifactDao::Confirm(
        jobpilot::core::ConnectionPool::Transaction &transaction,
        std::uint64_t userId,
        std::uint64_t artifactId)
    {
        // The fact gate is enforced here: a failed check (status 2) can never be
        // confirmed. Confirmation only sets the timestamp -- it never sends the
        // artifact anywhere.
        const jobpilot::core::ExecutionResult result =
            transaction.Execute(
                "UPDATE resume_artifacts SET confirmed_at = NOW() "
                "WHERE id = ? AND user_id = ? "
                "AND fact_check_status <> 2 "
                "AND confirmed_at IS NULL",
                {artifactId, userId});

        return result.affectedRows == 1;
    }
} // namespace jobpilot::service
