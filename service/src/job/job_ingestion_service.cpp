#include "jobpilot/service/job/job_ingestion_service.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <exception>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "jobpilot/core/logger.h"
#include "jobpilot/database/mysql_pool.h"
#include "jobpilot/service/job/job_extra_fields.h"
#include "jobpilot/service/job/job_quality.h"

namespace jobpilot::service
{
    namespace
    {
        constexpr std::size_t kMaxBatchSize = 100;
        constexpr std::size_t kMaxCompanyBytes = 128;
        constexpr std::size_t kMaxTitleBytes = 128;
        constexpr std::size_t kMaxLocationBytes = 64;
        constexpr std::size_t kMaxCurrencyBytes = 8;
        // jobs.description is TEXT (65,535 bytes of storage). The AUTHORITATIVE
        // application/ingest cap is the shared kMaxDescriptionBytes (49,152
        // UTF-8 bytes, job_quality.h); the column capacity is recorded here and
        // documented in docs/03 section 5.5.3 so the two can never contradict.
        constexpr std::size_t kDescriptionColumnBytes = 65535;
        static_assert(
            kMaxDescriptionBytes < kDescriptionColumnBytes,
            "the 48 KiB app cap must fit inside the TEXT column capacity");
        constexpr std::size_t kMaxSourceBytes = 32;
        constexpr std::size_t kMaxSourceUrlBytes = 255;
        constexpr std::size_t kMaxSkillBytes = 64;

        std::string FoldAsciiCase(
            const std::string &value)
        {
            std::string folded = value;

            std::transform(
                folded.begin(),
                folded.end(),
                folded.begin(),
                [](unsigned char character)
                {
                    return static_cast<char>(
                        std::tolower(character));
                });

            return folded;
        }

        bool HasValueWithin(
            const std::string &value,
            std::size_t maxBytes,
            bool required)
        {
            if (required && value.empty())
            {
                return false;
            }

            return value.size() <= maxBytes;
        }

        bool IsSupportedSource(
            const std::string &source)
        {
            return source == "manual" ||
                   source == "import" ||
                   source == "crawler";
        }

        bool ValidateJob(
            const JobIngestionItem &item)
        {
            const JobImportData &job = item.job;

            // 0015A option B: an empty employer is a fixable needs_review, so
            // only the byte cap is enforced here; the quality gate owns the
            // missing-employer verdict.
            if (!HasValueWithin(
                    job.company,
                    kMaxCompanyBytes,
                    false) ||
                !HasValueWithin(
                    job.title,
                    kMaxTitleBytes,
                    true) ||
                !HasValueWithin(
                    job.source,
                    kMaxSourceBytes,
                    true) ||
                !HasValueWithin(
                    job.sourceUrl,
                    kMaxSourceUrlBytes,
                    true) ||
                !HasValueWithin(
                    job.salaryCurrency,
                    kMaxCurrencyBytes,
                    true) ||
                !IsSupportedSource(job.source))
            {
                return false;
            }

            if (job.location.has_value() &&
                !HasValueWithin(
                    job.location.value(),
                    kMaxLocationBytes,
                    false))
            {
                return false;
            }

            if (job.description.has_value() &&
                !HasValueWithin(
                    job.description.value(),
                    kMaxDescriptionBytes,
                    false))
            {
                return false;
            }

            if (job.salaryMin.has_value() &&
                job.salaryMax.has_value() &&
                job.salaryMax.value() <
                    job.salaryMin.value())
            {
                return false;
            }

            if (!ValidateJobExtraFields(job))
            {
                return false;
            }

            std::set<std::string> skills;

            for (const JobRequirementImportData &requirement :
                 item.requirements)
            {
                if (!HasValueWithin(
                        requirement.skill,
                        kMaxSkillBytes,
                        true) ||
                    requirement.level < 1 ||
                    requirement.level > 3)
                {
                    return false;
                }

                if (!skills.insert(
                         FoldAsciiCase(
                             requirement.skill))
                         .second)
                {
                    return false;
                }
            }

            return true;
        }

        // 0015A: the pre-engine checks (byte caps, duplicate URLs, unsupported
        // source) now return a diagnosable reject code + field + reason instead
        // of an anonymous 400. Mirrors ValidateJob's order so the first real
        // problem wins.
        bool DiagnoseStructuralReject(
            const std::vector<JobIngestionItem> &jobs,
            std::string *code,
            std::string *field,
            std::string *reason)
        {
            if (jobs.empty())
            {
                *code = "batch_size_invalid";
                *field = "jobs";
                *reason = "jobs must not be empty";
                return true;
            }

            if (jobs.size() > kMaxBatchSize)
            {
                *code = "batch_size_invalid";
                *field = "jobs";
                *reason = "batch exceeds the 100 item limit";
                return true;
            }

            std::set<std::string> sourceUrls;

            for (const JobIngestionItem &item : jobs)
            {
                const JobImportData &job = item.job;

                const auto reject =
                    [code, field, reason](
                        const char *rejectCode,
                        const char *rejectField,
                        const std::string &message)
                {
                    *code = rejectCode;
                    *field = rejectField;
                    *reason = message;
                    return true;
                };

                if (job.company.empty())
                {
                    return reject(
                        "company_missing",
                        "company",
                        "employer is missing after normalisation");
                }

                if (job.company.size() > kMaxCompanyBytes)
                {
                    return reject(
                        "company_too_large",
                        "company",
                        "company exceeds 128 bytes");
                }

                if (job.title.empty())
                {
                    return reject(
                        "title_missing",
                        "title",
                        "title must not be empty");
                }

                if (job.title.size() > kMaxTitleBytes)
                {
                    return reject(
                        "title_too_large",
                        "title",
                        "title exceeds 128 bytes");
                }

                if (!HasValueWithin(
                        job.source,
                        kMaxSourceBytes,
                        true))
                {
                    return reject(
                        "source_unsupported",
                        "source",
                        "source is empty, too long or unsupported");
                }

                if (!IsSupportedSource(job.source))
                {
                    return reject(
                        "source_unsupported",
                        "source",
                        "source is not manual/import/crawler");
                }

                if (job.sourceUrl.empty())
                {
                    return reject(
                        "source_url_unusable",
                        "source_url",
                        "source_url must not be empty");
                }

                if (job.sourceUrl.size() > kMaxSourceUrlBytes)
                {
                    return reject(
                        "source_url_too_large",
                        "source_url",
                        "source_url exceeds 255 bytes");
                }

                if (!HasValueWithin(
                        job.salaryCurrency,
                        kMaxCurrencyBytes,
                        true))
                {
                    return reject(
                        "salary_currency_invalid",
                        "salary_currency",
                        "salary_currency is empty or over 8 bytes");
                }

                if (job.location.has_value() &&
                    !HasValueWithin(
                        job.location.value(),
                        kMaxLocationBytes,
                        false))
                {
                    return reject(
                        "location_too_large",
                        "location",
                        "location exceeds 64 bytes");
                }

                if (job.description.has_value() &&
                    !HasValueWithin(
                        job.description.value(),
                        kMaxDescriptionBytes,
                        false))
                {
                    return reject(
                        "description_too_large",
                        "description",
                        "description exceeds the 49152 byte cap "
                        "(not truncated)");
                }

                if (job.salaryMin.has_value() &&
                    job.salaryMax.has_value() &&
                    job.salaryMax.value() <
                        job.salaryMin.value())
                {
                    return reject(
                        "salary_range_invalid",
                        "salary_max",
                        "salary_max is below salary_min");
                }

                if (!ValidateJobExtraFields(job))
                {
                    return reject(
                        "extra_field_invalid",
                        "deadline",
                        "0014 extra fields are out of range");
                }

                for (const JobRequirementImportData &requirement :
                     item.requirements)
                {
                    if (requirement.skill.empty())
                    {
                        return reject(
                            "skill_missing",
                            "requirements.skill",
                            "requirement skill must not be empty");
                    }

                    if (requirement.skill.size() > kMaxSkillBytes)
                    {
                        return reject(
                            "skill_too_large",
                            "requirements.skill",
                            "requirement skill exceeds 64 bytes");
                    }

                    if (requirement.level < 1 ||
                        requirement.level > 3)
                    {
                        return reject(
                            "skill_level_invalid",
                            "requirements.level",
                            "requirement level must be 1..3");
                    }
                }

                if (!sourceUrls
                         .insert(
                             FoldAsciiCase(
                                 job.sourceUrl))
                         .second)
                {
                    return reject(
                        "duplicate_source_url",
                        "source_url",
                        "duplicate source_url inside one batch");
                }
            }

            return false;
        }

        // Byte size that triggered a size-reject, so the response can report
        // limit_bytes/actual_bytes (docs/04 5.4).
        std::size_t ActualRejectBytes(
            const std::vector<JobIngestionItem> &jobs,
            const std::vector<JobQualityReport> &reports,
            const std::string &code)
        {
            for (std::size_t index = 0;
                 index < jobs.size() && index < reports.size();
                 ++index)
            {
                if (!reports[index].rejected ||
                    reports[index].diagnosticCode != code)
                {
                    continue;
                }

                const JobImportData &job = jobs[index].job;

                if (code == "description_too_large")
                {
                    return job.description.has_value()
                               ? job.description->size()
                               : 0;
                }

                if (code == "company_too_large")
                {
                    return job.company.size();
                }

                if (code == "title_too_large")
                {
                    return job.title.size();
                }

                if (code == "source_url_too_large")
                {
                    return job.sourceUrl.size();
                }

                if (code == "location_too_large")
                {
                    return job.location.has_value()
                               ? job.location->size()
                               : 0;
                }

                return 0;
            }

            return 0;
        }
    } // namespace

    JobIngestionService::JobIngestionService(
        IJobIngestionDao &jobIngestionDao,
        jobpilot::core::ConnectionPool &pool,
        PlatformNameRegistry platforms)
        : jobIngestionDao_(jobIngestionDao),
          pool_(pool),
          platforms_(std::move(platforms))
    {
    }

    bool JobIngestionService::Validate(
        const std::vector<JobIngestionItem> &jobs)
    {
        if (jobs.empty() ||
            jobs.size() > kMaxBatchSize)
        {
            return false;
        }

        std::set<std::string> sourceUrls;

        for (const JobIngestionItem &item : jobs)
        {
            if (!ValidateJob(item))
            {
                return false;
            }

            if (!sourceUrls.insert(
                     FoldAsciiCase(
                         item.job.sourceUrl))
                     .second)
            {
                return false;
            }
        }

        return true;
    }

    ServiceResult<JobIngestionResult>
    JobIngestionService::Import(
        const std::vector<JobIngestionItem> &jobs)
    {
        std::vector<JobImportData> jobData;
        jobData.reserve(jobs.size());

        for (const JobIngestionItem &item : jobs)
        {
            jobData.push_back(item.job);
        }

        // Deterministic pre-ingest verdicts: per item, then the batch-level
        // duplicate finding (ExecPlan 0015A section 6.1).
        std::vector<JobQualityReport> reports;
        reports.reserve(jobData.size());

        for (const JobImportData &job : jobData)
        {
            reports.push_back(
                EvaluateJobQuality(job, platforms_));
        }

        MarkDuplicateContentInBatch(jobData, &reports);

        JobIngestionResult result;
        result.total = jobs.size();
        result.quality = SummarizeJobQuality(jobData, reports);

        // A rejected item cannot be safely normalised: the request fails with
        // a diagnostic and nothing is written (ExecPlan 0015A section 6.1).
        if (result.quality.rejected > 0)
        {
            result.quality.diagnosticActualBytes =
                ActualRejectBytes(
                    jobs,
                    reports,
                    result.quality.diagnosticCode);

            return ServiceResult<JobIngestionResult>{
                ErrorCode::kInvalidParam,
                std::move(result)};
        }

        if (!Validate(jobs))
        {
            std::string code;
            std::string field;
            std::string reason;

            if (DiagnoseStructuralReject(
                    jobs,
                    &code,
                    &field,
                    &reason))
            {
                result.quality.diagnosticCode = code;
                result.quality.diagnosticField = field;
                result.quality.diagnosticReason = reason;
            }

            return ServiceResult<JobIngestionResult>{
                ErrorCode::kInvalidParam,
                std::move(result)};
        }

        try
        {
            result.created = 0;
            result.updated = 0;

            auto transaction =
                pool_.BeginTransaction();

            for (std::size_t index = 0;
                 index < jobs.size();
                 ++index)
            {
                const JobIngestionItem &item = jobs[index];

                const JobUpsertResult upserted =
                    jobIngestionDao_.UpsertJob(
                        transaction,
                        item.job);

                if (upserted.status ==
                    JobUpsertStatus::kCreated)
                {
                    ++result.created;
                }
                else
                {
                    ++result.updated;
                }

                jobIngestionDao_.ReplaceRequirements(
                    transaction,
                    upserted.jobId,
                    item.requirements);

                // 0015A: verdict and findings are written in the same
                // transaction as the row they belong to.
                jobIngestionDao_.SetReviewStatus(
                    transaction,
                    upserted.jobId,
                    reports[index].reviewStatus);

                jobIngestionDao_.InsertReviewItems(
                    transaction,
                    upserted.jobId,
                    reports[index].findings);
            }

            transaction.Commit();

            return ServiceResult<JobIngestionResult>{
                ErrorCode::kSuccess,
                std::move(result)};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "job ingestion failed: {}",
                error.what());

            return ServiceResult<JobIngestionResult>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

} // namespace jobpilot::service
