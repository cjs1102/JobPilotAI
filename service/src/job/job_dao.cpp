#include "jobpilot/service/job/job_dao.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
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
            const JobSearchFilter &filter)
        {
            JobFilter result;
            result.sql = " WHERE j.status = 1";

            if (filter.location.has_value())
            {
                result.sql += " AND j.location = ?";
                result.params.emplace_back(
                    filter.location.value());
            }

            if (filter.keyword.has_value())
            {
                const std::string pattern =
                    "%" + EscapeLikeLiteral(
                              filter.keyword.value()) +
                    "%";

                result.sql +=
                    " AND ("
                    "j.title LIKE ? ESCAPE '!' "
                    "OR j.company LIKE ? ESCAPE '!' "
                    "OR COALESCE(j.description, '') "
                    "LIKE ? ESCAPE '!'"
                    ")";
                result.params.emplace_back(pattern);
                result.params.emplace_back(pattern);
                result.params.emplace_back(pattern);
            }

            if (filter.skill.has_value())
            {
                result.sql +=
                    " AND EXISTS ("
                    "SELECT 1 FROM job_requirements jr "
                    "WHERE jr.job_id = j.id "
                    "AND jr.skill = ?)";
                result.params.emplace_back(
                    filter.skill.value());
            }

            if (filter.salaryMin.has_value())
            {
                result.sql +=
                    " AND j.salary_max IS NOT NULL "
                    "AND j.salary_max >= ?";
                result.params.emplace_back(
                    static_cast<std::uint64_t>(
                        filter.salaryMin.value()));
            }

            if (filter.salaryMax.has_value())
            {
                result.sql +=
                    " AND j.salary_min IS NOT NULL "
                    "AND j.salary_min <= ?";
                result.params.emplace_back(
                    static_cast<std::uint64_t>(
                        filter.salaryMax.value()));
            }

            return result;
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
        const JobSearchFilter &filter,
        std::size_t limit,
        std::size_t offset)
    {
        const JobFilter sqlFilter =
            BuildJobFilter(filter);

        std::vector<jobpilot::core::SqlParam> params =
            sqlFilter.params;
        params.emplace_back(
            static_cast<std::uint64_t>(limit));
        params.emplace_back(
            static_cast<std::uint64_t>(offset));

        const jobpilot::core::Result result =
            pool_.Query(
                "SELECT j.id, j.company, j.title, j.location, "
                "j.salary_min, j.salary_max, j.salary_currency, "
                "j.source, j.create_time, j.update_time "
                "FROM jobs j" +
                    sqlFilter.sql +
                    " ORDER BY j.create_time DESC, j.id DESC "
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
        const JobSearchFilter &filter)
    {
        const JobFilter sqlFilter =
            BuildJobFilter(filter);

        const jobpilot::core::Result result =
            pool_.Query(
                "SELECT COUNT(*) FROM jobs j" +
                    sqlFilter.sql,
                sqlFilter.params);

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

    std::vector<JobSummary> JobDao::SearchRankingCandidates(
        const JobSearchFilter &filter,
        std::size_t limit)
    {
        const JobFilter sqlFilter =
            BuildJobFilter(filter);

        std::vector<jobpilot::core::SqlParam> params =
            sqlFilter.params;
        params.emplace_back(
            static_cast<std::uint64_t>(limit));

        const jobpilot::core::Result result =
            pool_.Query(
                "SELECT j.id, j.company, j.title, j.location, "
                "j.salary_min, j.salary_max, j.salary_currency, "
                "j.source, j.create_time, j.update_time "
                "FROM jobs j" +
                    sqlFilter.sql +
                    " ORDER BY j.create_time DESC, j.id DESC "
                    "LIMIT ?",
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

    std::size_t JobDao::CountRankingCandidates(
        const JobSearchFilter &filter)
    {
        return CountJobs(filter);
    }

    std::vector<JobRequirementsRecord>
    JobDao::ListRequirementsForJobs(
        const std::vector<std::uint64_t> &jobIds)
    {
        std::vector<JobRequirementsRecord> records;

        if (jobIds.empty())
        {
            return records;
        }

        constexpr std::size_t kChunkSize = 500;
        std::unordered_map<
            std::uint64_t,
            std::size_t> recordIndex;

        for (std::size_t begin = 0;
             begin < jobIds.size();
             begin += kChunkSize)
        {
            const std::size_t end =
                std::min(
                    jobIds.size(),
                    begin + kChunkSize);
            std::string placeholders;
            std::vector<jobpilot::core::SqlParam> params;
            params.reserve(end - begin);

            for (std::size_t index = begin;
                 index < end;
                 ++index)
            {
                if (!placeholders.empty())
                {
                    placeholders += ", ";
                }

                placeholders += "?";
                params.emplace_back(jobIds[index]);
            }

            const jobpilot::core::Result result =
                pool_.Query(
                    "SELECT job_id, skill, level "
                    "FROM job_requirements "
                    "WHERE job_id IN (" +
                        placeholders +
                        ") "
                        "ORDER BY job_id ASC, skill ASC",
                    params);

            for (std::size_t row = 0;
                 row < result.rowCount();
                 ++row)
            {
                const auto &values = result.row(row);

                if (values.size() != 3)
                {
                    throw std::runtime_error(
                        "job requirements query returned an "
                        "unexpected column count");
                }

                const std::uint64_t jobId =
                    ParseUint64(
                        values[0],
                        "job_requirements.job_id");
                const auto found =
                    recordIndex.find(jobId);
                std::size_t recordPosition = 0;

                if (found == recordIndex.end())
                {
                    recordPosition = records.size();
                    recordIndex.emplace(
                        jobId,
                        recordPosition);
                    JobRequirementsRecord record;
                    record.jobId = jobId;
                    records.push_back(
                        std::move(record));
                }
                else
                {
                    recordPosition = found->second;
                }

                records[recordPosition]
                    .requirements.push_back(
                        JobRequirement{
                            RequireValue(
                                values[1],
                                "job_requirements.skill"),
                            ParseInt32(
                                values[2],
                                "job_requirements.level")});
            }
        }

        return records;
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

    bool JobDao::ActiveJobExists(
        jobpilot::core::ConnectionPool::Transaction &transaction,
        std::uint64_t jobId)
    {
        const jobpilot::core::Result result =
            transaction.Query(
                "SELECT id FROM jobs "
                "WHERE id = ? AND status = 1 "
                "LIMIT 1",
                {jobId});

        return result.rowCount() == 1;
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
