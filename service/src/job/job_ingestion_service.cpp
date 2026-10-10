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

namespace jobpilot::service
{
    namespace
    {
        constexpr std::size_t kMaxBatchSize = 100;
        constexpr std::size_t kMaxCompanyBytes = 128;
        constexpr std::size_t kMaxTitleBytes = 128;
        constexpr std::size_t kMaxLocationBytes = 64;
        constexpr std::size_t kMaxCurrencyBytes = 8;
        constexpr std::size_t kMaxDescriptionBytes = 65535;
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

            if (!HasValueWithin(
                    job.company,
                    kMaxCompanyBytes,
                    true) ||
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
    } // namespace

    JobIngestionService::JobIngestionService(
        IJobIngestionDao &jobIngestionDao,
        jobpilot::core::ConnectionPool &pool)
        : jobIngestionDao_(jobIngestionDao),
          pool_(pool)
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
        if (!Validate(jobs))
        {
            return ServiceResult<JobIngestionResult>{
                ErrorCode::kInvalidParam,
                {}};
        }

        try
        {
            JobIngestionResult result;
            result.total = jobs.size();

            auto transaction =
                pool_.BeginTransaction();

            for (const JobIngestionItem &item : jobs)
            {
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
