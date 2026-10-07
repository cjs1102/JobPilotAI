#include "jobpilot/service/job/job_dao.h"

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
        struct JobFilter
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

        std::uint32_t ParseUint32(
            const jobpilot::core::Result::Value &value,
            const std::string &field)
        {
            const std::string text =
                RequireValue(value, field);

            std::size_t parsed = 0;
            const unsigned long long result =
                std::stoull(text, &parsed);

            if (parsed != text.size() ||
                result >
                    std::numeric_limits<
                        std::uint32_t>::max())
            {
                throw std::runtime_error(
                    field + " contains an invalid value");
            }

            return static_cast<std::uint32_t>(
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

        std::string EscapeLikeLiteral(
            const std::string &value)
        {
            std::string escaped;
            escaped.reserve(value.size());

            for (char character : value)
            {
                if (character == '!' ||
                    character == '%' ||
                    character == '_')
                {
                    escaped.push_back('!');
                }

                escaped.push_back(character);
            }

            return escaped;
        }

        JobFilter BuildJobFilter(
            const std::optional<std::string> &keyword,
            const std::optional<std::string> &location)
        {
            JobFilter filter;
            filter.sql = " WHERE status = 1";

            if (location.has_value())
            {
                filter.sql += " AND location = ?";
                filter.params.emplace_back(
                    location.value());
            }

            if (keyword.has_value())
            {
                const std::string pattern =
                    "%" + EscapeLikeLiteral(
                              keyword.value()) +
                    "%";

                filter.sql +=
                    " AND ("
                    "title LIKE ? ESCAPE '!' "
                    "OR company LIKE ? ESCAPE '!' "
                    "OR COALESCE(description, '') "
                    "LIKE ? ESCAPE '!'"
                    ")";
                filter.params.emplace_back(pattern);
                filter.params.emplace_back(pattern);
                filter.params.emplace_back(pattern);
            }

            return filter;
        }

        JobSummary MakeJobSummary(
            const jobpilot::core::Result::Row &row)
        {
            if (row.size() != 10)
            {
                throw std::runtime_error(
                    "job query returned an unexpected column count");
            }

            JobSummary summary;
            summary.id =
                ParseUint64(row[0], "jobs.id");
            summary.company =
                RequireValue(row[1], "jobs.company");
            summary.title =
                RequireValue(row[2], "jobs.title");

            if (row[3].has_value())
            {
                summary.location = row[3].value();
            }

            if (row[4].has_value())
            {
                summary.salaryMin =
                    ParseUint32(
                        row[4],
                        "jobs.salary_min");
            }

            if (row[5].has_value())
            {
                summary.salaryMax =
                    ParseUint32(
                        row[5],
                        "jobs.salary_max");
            }

            summary.salaryCurrency =
                RequireValue(
                    row[6],
                    "jobs.salary_currency");
            summary.source =
                RequireValue(row[7], "jobs.source");
            summary.createTime =
                RequireValue(row[8], "jobs.create_time");
            summary.updateTime =
                RequireValue(row[9], "jobs.update_time");

            return summary;
        }
    } // namespace

    JobDao::JobDao(
        jobpilot::core::ConnectionPool &pool)
        : pool_(pool)
    {
    }

    std::vector<JobSummary> JobDao::SearchJobs(
        const std::optional<std::string> &keyword,
        const std::optional<std::string> &location,
        std::size_t limit,
        std::size_t offset)
    {
        const JobFilter filter =
            BuildJobFilter(keyword, location);

        std::vector<jobpilot::core::SqlParam> params =
            filter.params;
        params.emplace_back(
            static_cast<std::uint64_t>(limit));
        params.emplace_back(
            static_cast<std::uint64_t>(offset));

        const jobpilot::core::Result result =
            pool_.Query(
                "SELECT id, company, title, location, "
                "salary_min, salary_max, salary_currency, "
                "source, create_time, update_time "
                "FROM jobs" +
                    filter.sql +
                    " ORDER BY update_time DESC, id DESC "
                    "LIMIT ? OFFSET ?",
                params);

        std::vector<JobSummary> jobs;
        jobs.reserve(result.rowCount());

        for (std::size_t row = 0;
             row < result.rowCount();
             ++row)
        {
            jobs.push_back(
                MakeJobSummary(result.row(row)));
        }

        return jobs;
    }

    std::size_t JobDao::CountJobs(
        const std::optional<std::string> &keyword,
        const std::optional<std::string> &location)
    {
        const JobFilter filter =
            BuildJobFilter(keyword, location);

        const jobpilot::core::Result result =
            pool_.Query(
                "SELECT COUNT(*) FROM jobs" +
                    filter.sql,
                filter.params);

        if (result.rowCount() != 1)
        {
            throw std::runtime_error(
                "job count query returned an unexpected row count");
        }

        return static_cast<std::size_t>(
            ParseUint64(
                result.value(0, 0),
                "jobs.count"));
    }

    std::optional<JobDetail> JobDao::FindActiveJobById(
        std::uint64_t jobId)
    {
        const jobpilot::core::Result result =
            pool_.Query(
                "SELECT id, company, title, location, "
                "salary_min, salary_max, salary_currency, "
                "source, create_time, update_time, "
                "description, source_url, status "
                "FROM jobs "
                "WHERE id = ? AND status = 1 "
                "LIMIT 1",
                {jobId});

        if (result.rowCount() == 0)
        {
            return std::nullopt;
        }

        const auto &row = result.row(0);

        if (row.size() != 13)
        {
            throw std::runtime_error(
                "job detail query returned an unexpected column count");
        }

        JobDetail detail;
        detail.summary =
            MakeJobSummary(
                jobpilot::core::Result::Row(
                    row.begin(),
                    row.begin() + 10));

        if (row[10].has_value())
        {
            detail.description = row[10].value();
        }

        if (row[11].has_value())
        {
            detail.sourceUrl = row[11].value();
        }

        detail.status =
            ParseInt32(row[12], "jobs.status");

        return detail;
    }

    std::vector<JobRequirement> JobDao::ListRequirements(
        std::uint64_t jobId)
    {
        const jobpilot::core::Result result =
            pool_.Query(
                "SELECT skill, level "
                "FROM job_requirements "
                "WHERE job_id = ? "
                "ORDER BY skill ASC",
                {jobId});

        std::vector<JobRequirement> requirements;
        requirements.reserve(result.rowCount());

        for (std::size_t row = 0;
             row < result.rowCount();
             ++row)
        {
            const auto &values = result.row(row);

            if (values.size() != 2)
            {
                throw std::runtime_error(
                    "job requirement query returned an unexpected "
                    "column count");
            }

            JobRequirement requirement;
            requirement.skill =
                RequireValue(
                    values[0],
                    "job_requirements.skill");
            requirement.level =
                ParseInt32(
                    values[1],
                    "job_requirements.level");
            requirements.push_back(
                std::move(requirement));
        }

        return requirements;
    }

} // namespace jobpilot::service
