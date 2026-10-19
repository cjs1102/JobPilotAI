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
        // 0015 B3: the public list SELECT prefix (projection + FROM) and the
        // ORDER BY are shared by `SearchJobs`, `SearchRankingCandidates` and the
        // strict candidate window, so row shape and row order can never diverge
        // between the page and the strict-filtered window.
        constexpr const char *kJobSummarySelect =
            "SELECT j.id, j.company, j.title, j.location, "
            "j.salary_min, j.salary_max, j.salary_currency, "
            "j.source, j.create_time, j.update_time, "
            "j.published_at, j.last_seen_at, j.last_verified_at, "
            "j.source_status, j.closed_at, "
            "j.education_required, j.experience_years_min, "
            "j.experience_years_max, j.job_nature, j.deadline, "
            "j.deadline_date_only, "
            "TIMESTAMPDIFF(SECOND, j.last_seen_at, NOW()), "
            "j.source_url, j.apply_url "
            "FROM jobs j";

        // Whitelisted sort orders only; never string-interpolated from the
        // request. kDeadlineAsc is index-backed (docs/03 section 9.2) and
        // therefore uses the consistent "ASC, ASC" direction (NULLs first).
        const char *ListOrderBy(
            JobSort sort)
        {
            switch (sort)
            {
            case JobSort::kDeadlineAsc:
                return " ORDER BY j.deadline ASC, j.id ASC ";
            case JobSort::kEducationAsc:
                return " ORDER BY j.education_required ASC, j.id ASC ";
            case JobSort::kCreatedDesc:
                break;
            }

            return " ORDER BY j.create_time DESC, j.id DESC ";
        }

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

        std::int64_t ParseInt64(
            const jobpilot::core::Result::Value &value,
            const std::string &field)
        {
            const std::string text =
                RequireValue(value, field);

            std::size_t parsed = 0;
            const long long result =
                std::stoll(text, &parsed);

            if (parsed != text.size())
            {
                throw std::runtime_error(
                    field + " contains invalid characters");
            }

            return static_cast<std::int64_t>(result);
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
            result.sql =
                " WHERE j.status = 1 AND j.closed_at IS NULL";

            // 0015A: only clean rows are public. The predicate is the stage's
            // single behavioural change and is switchable (ExecPlan 0015A
            // sections 5.1/6.4).
            if (!filter.includeNeedsReview)
            {
                result.sql += " AND j.review_status = 0";
            }

            if (!filter.includeStale)
            {
                // Legacy rows without an observation stay visible (ADR-0010
                // section 8); only rows observed longer ago than the aging
                // window count as stale.
                result.sql +=
                    " AND (j.last_seen_at IS NULL "
                    "OR j.last_seen_at >= "
                    "TIMESTAMPADD(SECOND, ?, NOW()))";
                result.params.emplace_back(
                    static_cast<std::int64_t>(
                        -kAgingMaxAgeSeconds));
            }

            if (filter.postedWithinDays.has_value())
            {
                result.sql +=
                    " AND j.published_at IS NOT NULL "
                    "AND j.published_at >= "
                    "TIMESTAMPADD(DAY, ?, NOW())";
                result.params.emplace_back(
                    -static_cast<std::int64_t>(
                        filter.postedWithinDays.value()));
            }

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

            // 0015A focus fields. Unknown (NULL) education stays visible; a NULL
            // deadline satisfies neither range bound but stays visible under
            // openOnly (docs/04 section 5.1).
            if (filter.educationMax.has_value())
            {
                result.sql +=
                    " AND (j.education_required IS NULL "
                    "OR j.education_required <= ?)";
                result.params.emplace_back(
                    static_cast<std::uint64_t>(
                        filter.educationMax.value()));
            }

            if (filter.jobNature.has_value())
            {
                result.sql += " AND j.job_nature = ?";
                result.params.emplace_back(
                    static_cast<std::uint64_t>(
                        filter.jobNature.value()));
            }

            if (filter.deadlineAfter.has_value())
            {
                result.sql +=
                    " AND j.deadline IS NOT NULL "
                    "AND j.deadline > ?";
                result.params.emplace_back(
                    filter.deadlineAfter.value());
            }

            if (filter.deadlineBefore.has_value())
            {
                result.sql +=
                    " AND j.deadline IS NOT NULL "
                    "AND j.deadline < ?";
                result.params.emplace_back(
                    filter.deadlineBefore.value());
            }

            if (filter.openOnly)
            {
                // Deadline is display/urgency only: this predicate never closes
                // a job, changes status, or writes freshness (F13).
                result.sql +=
                    " AND (j.deadline IS NULL "
                    "OR j.deadline >= NOW())";
            }

            return result;
        }

        constexpr std::size_t kNoSourceUrlColumn =
            static_cast<std::size_t>(-1);

        JobSummary MakeJobSummary(
            const jobpilot::core::Result::Row &row,
            std::size_t sourceUrlColumn = kNoSourceUrlColumn)
        {
            const std::size_t expectedColumns =
                sourceUrlColumn == kNoSourceUrlColumn ? 22U : 24U;

            if (row.size() != expectedColumns)
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

            if (row[10].has_value())
            {
                summary.publishedAt = row[10].value();
            }

            if (row[11].has_value())
            {
                summary.lastSeenAt = row[11].value();
            }

            if (row[12].has_value())
            {
                summary.lastVerifiedAt = row[12].value();
            }

            if (row[13].has_value())
            {
                summary.sourceStatus = row[13].value();
            }

            const bool closed = row[14].has_value();

            if (row[15].has_value())
            {
                summary.educationRequired =
                    ParseUint32(
                        row[15],
                        "jobs.education_required");
            }

            if (row[16].has_value())
            {
                summary.experienceYearsMin =
                    ParseUint32(
                        row[16],
                        "jobs.experience_years_min");
            }

            if (row[17].has_value())
            {
                summary.experienceYearsMax =
                    ParseUint32(
                        row[17],
                        "jobs.experience_years_max");
            }

            if (row[18].has_value())
            {
                summary.jobNature =
                    ParseUint32(
                        row[18],
                        "jobs.job_nature");
            }

            if (row[19].has_value())
            {
                summary.deadline = row[19].value();
            }

            if (row[20].has_value())
            {
                // NULL keeps the kind unknown (docs/03 section 9.1).
                summary.deadlineDateOnly =
                    ParseInt32(
                        row[20],
                        "jobs.deadline_date_only") != 0;
            }

            std::optional<std::int64_t> ageSeconds;

            if (row[21].has_value())
            {
                ageSeconds =
                    ParseInt64(
                        row[21],
                        "jobs.last_seen_age_seconds");
            }

            summary.freshness =
                ClassifyFreshness(closed, ageSeconds);

            if (sourceUrlColumn != kNoSourceUrlColumn &&
                row[sourceUrlColumn].has_value())
            {
                summary.sourceUrl = row[sourceUrlColumn].value();
            }

            if (sourceUrlColumn != kNoSourceUrlColumn &&
                row[sourceUrlColumn + 1].has_value())
            {
                summary.applyUrl = row[sourceUrlColumn + 1].value();
            }

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
                std::string(kJobSummarySelect) +
                    sqlFilter.sql +
                    ListOrderBy(filter.sort) +
                    "LIMIT ? OFFSET ?",
                params);


        std::vector<JobSummary> jobs;
        jobs.reserve(result.rowCount());

        for (std::size_t row = 0;
             row < result.rowCount();
             ++row)
        {
            jobs.push_back(
                MakeJobSummary(result.row(row), 22));
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
                std::string(kJobSummarySelect) +
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
                MakeJobSummary(result.row(row), 22));
        }

        return jobs;
    }

    // 0015 B3: the strict candidate window. Same predicate and the SAME ORDER BY
    // as the public list page, so the strict-filtered set and its total describe
    // exactly the rows a caller can walk.
    std::vector<JobSummary> JobDao::SearchListWindow(
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
                std::string(kJobSummarySelect) +
                    sqlFilter.sql +
                    ListOrderBy(filter.sort) +
                    "LIMIT ?",
                params);

        std::vector<JobSummary> jobs;
        jobs.reserve(result.rowCount());

        for (std::size_t row = 0;
             row < result.rowCount();
             ++row)
        {
            jobs.push_back(
                MakeJobSummary(result.row(row), 22));
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

    std::optional<JobDetail> JobDao::FindPublicJobById(
        std::uint64_t jobId,
        bool includeNeedsReview)
    {
        return QueryJobDetail(jobId, includeNeedsReview);
    }

    std::optional<JobDetail> JobDao::FindReviewableJobById(
        std::uint64_t jobId)
    {
        // Internal/ops semantics: the review state of the row is irrelevant, the
        // row only has to be active. Kept as a separate entry point so no call
        // site can confuse "the public may read this" with "an operator or the
        // worker may process this".
        return QueryJobDetail(jobId, true);
    }

    std::optional<JobDetail> JobDao::QueryJobDetail(
        std::uint64_t jobId,
        bool includeNeedsReview)
    {
        std::string sql =
            "SELECT id, company, title, location, "
            "salary_min, salary_max, salary_currency, "
            "source, create_time, update_time, "
            "published_at, last_seen_at, last_verified_at, "
            "source_status, closed_at, "
            "education_required, experience_years_min, "
            "experience_years_max, job_nature, deadline, "
            "deadline_date_only, "
            "TIMESTAMPDIFF(SECOND, last_seen_at, NOW()), "
            "description, source_url, apply_url, status "
            "FROM jobs "
            "WHERE id = ? AND status = 1 ";

        // 0015A: a public read never returns a row that is still queued for
        // review. The response is the same as for a missing row.
        if (!includeNeedsReview)
        {
            sql += "AND review_status = 0 ";
        }

        sql += "LIMIT 1";

        const jobpilot::core::Result result =
            pool_.Query(sql, {jobId});

        if (result.rowCount() == 0)
        {
            return std::nullopt;
        }

        const auto &row = result.row(0);

        if (row.size() != 26)
        {
            throw std::runtime_error(
                "job detail query returned an unexpected column count");
        }

        JobDetail detail;
        detail.summary =
            MakeJobSummary(
                jobpilot::core::Result::Row(
                    row.begin(),
                    row.begin() + 22));

        if (row[22].has_value())
        {
            detail.description = row[22].value();
        }

        if (row[23].has_value())
        {
            detail.sourceUrl = row[23].value();
        }

        // 0015A: the summary embedded in the detail must expose the same
        // source_url as the list projection (acceptance: identical value).
        detail.summary.sourceUrl = detail.sourceUrl;

        // 0020: the detail projection carries apply_url after source_url.
        if (row[24].has_value())
        {
            detail.summary.applyUrl = row[24].value();
        }

        detail.status =
            ParseInt32(row[25], "jobs.status");

        return detail;
    }

    bool JobDao::PublicJobExists(
        jobpilot::core::ConnectionPool::Transaction &transaction,
        std::uint64_t jobId,
        bool includeNeedsReview)
    {
        std::string sql =
            "SELECT id FROM jobs "
            "WHERE id = ? AND status = 1 ";

        // 0015A: an application may only reference a publicly visible job, so a
        // queued row fails this check with the same error as a missing one.
        if (!includeNeedsReview)
        {
            sql += "AND review_status = 0 ";
        }

        sql += "LIMIT 1";

        const jobpilot::core::Result result =
            transaction.Query(sql, {jobId});

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
