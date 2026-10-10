#include "jobpilot/service/job/job_review.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <exception>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "jobpilot/core/logger.h"
#include "jobpilot/service/job/job_quality.h"

namespace jobpilot::service
{
    namespace
    {
        constexpr std::size_t kMaxQueuePageSize = 100;
        constexpr std::size_t kBackfillSampleLimit = 20;
        constexpr std::size_t kBackfillCandidateLimit = 100000;
        constexpr std::size_t kMaxCompanyBytes = 128;
        constexpr std::size_t kMaxTitleBytes = 128;
        constexpr std::size_t kMaxSourceUrlBytes = 255;

        std::string Trim(
            const std::string &value)
        {
            std::size_t begin = 0;
            std::size_t end = value.size();

            while (begin < end &&
                   std::isspace(
                       static_cast<unsigned char>(
                           value[begin])) != 0)
            {
                ++begin;
            }

            while (end > begin &&
                   std::isspace(
                       static_cast<unsigned char>(
                           value[end - 1])) != 0)
            {
                --end;
            }

            return value.substr(begin, end - begin);
        }

        bool IsAbsoluteHttpUrl(
            const std::string &value)
        {
            return value.rfind("http://", 0) == 0 ||
                   value.rfind("https://", 0) == 0;
        }

        bool ParseStatus(
            const std::string &value,
            std::int32_t *status)
        {
            if (value.empty() || value == "needs_review")
            {
                *status = 1;
                return true;
            }

            if (value == "quarantined")
            {
                *status = 2;
                return true;
            }

            if (value == "clean")
            {
                *status = 0;
                return true;
            }

            return false;
        }

        const char *ResolutionFor(
            JobReviewDecision decision)
        {
            switch (decision)
            {
            case JobReviewDecision::kApprove:
                return "approved";
            case JobReviewDecision::kFix:
                return "fixed";
            case JobReviewDecision::kReject:
                return "rejected";
            }

            return "approved";
        }

        // §5.5 contract re-validation for a `fix` payload. Returns false and
        // fills the diagnostic when a provided field would be unusable.
        bool ValidateFix(
            const JobReviewFix &fix,
            const PlatformNameRegistry &platforms,
            std::string *code,
            std::string *field,
            std::string *reason)
        {
            const auto reject =
                [code, field, reason](
                    const char *rejectCode,
                    const char *rejectField,
                    const std::string &message)
            {
                *code = rejectCode;
                *field = rejectField;
                *reason = message;
                return false;
            };

            if (fix.company.has_value())
            {
                const std::string company =
                    Trim(fix.company.value());

                if (company.empty())
                {
                    return reject(
                        "company_missing",
                        "company",
                        "employer must not be empty");
                }

                if (company.size() > kMaxCompanyBytes)
                {
                    return reject(
                        "company_too_large",
                        "company",
                        "company exceeds 128 bytes");
                }

                if (platforms.Contains(company))
                {
                    return reject(
                        "employer_is_platform",
                        "company",
                        "employer normalises to a registered "
                        "platform name; the platform is not the "
                        "employer");
                }
            }

            if (fix.title.has_value())
            {
                const std::string title =
                    Trim(fix.title.value());

                if (title.empty())
                {
                    return reject(
                        "title_missing",
                        "title",
                        "title must not be empty");
                }

                if (title.size() > kMaxTitleBytes)
                {
                    return reject(
                        "title_too_large",
                        "title",
                        "title exceeds 128 bytes");
                }
            }

            if (fix.sourceUrl.has_value())
            {
                const std::string url =
                    Trim(fix.sourceUrl.value());

                if (!IsAbsoluteHttpUrl(url))
                {
                    return reject(
                        "source_url_unusable",
                        "source_url",
                        "source_url must be an absolute http(s) URL");
                }

                if (url.size() > kMaxSourceUrlBytes)
                {
                    return reject(
                        "source_url_too_large",
                        "source_url",
                        "source_url exceeds 255 bytes");
                }
            }

            if (fix.description.has_value() &&
                fix.description->size() > kMaxDescriptionBytes)
            {
                return reject(
                    "description_too_large",
                    "description",
                    "description is " +
                        std::to_string(
                            fix.description->size()) +
                        " bytes, over the " +
                        std::to_string(kMaxDescriptionBytes) +
                        " byte cap (not truncated)");
            }

            return true;
        }

        JobImportData ToJobImportData(
            const JobReviewCandidate &candidate)
        {
            JobImportData job;
            job.company = candidate.company;
            job.title = candidate.title;
            job.location = candidate.location;
            job.salaryMin = candidate.salaryMin;
            job.salaryMax = candidate.salaryMax;
            job.salaryCurrency = candidate.salaryCurrency;
            job.description = candidate.description;
            job.source = candidate.source;
            job.sourceUrl = candidate.sourceUrl;
            return job;
        }
    } // namespace

    const char *ToString(JobReviewDecision decision)
    {
        switch (decision)
        {
        case JobReviewDecision::kApprove:
            return "approve";
        case JobReviewDecision::kFix:
            return "fix";
        case JobReviewDecision::kReject:
            return "reject";
        }

        return "approve";
    }

    bool ParseJobReviewDecision(
        const std::string &value,
        JobReviewDecision *decision)
    {
        if (value == "approve")
        {
            *decision = JobReviewDecision::kApprove;
            return true;
        }

        if (value == "fix")
        {
            *decision = JobReviewDecision::kFix;
            return true;
        }

        if (value == "reject")
        {
            *decision = JobReviewDecision::kReject;
            return true;
        }

        return false;
    }

    JobReviewService::JobReviewService(
        IJobReviewDao &dao,
        PlatformNameRegistry platforms)
        : dao_(dao),
          platforms_(std::move(platforms))
    {
    }

    ServiceResult<JobReviewQueuePage> JobReviewService::ListQueue(
        const std::string &status,
        std::size_t page,
        std::size_t pageSize)
    {
        std::int32_t reviewStatus = 1;

        if (!ParseStatus(status, &reviewStatus))
        {
            return ServiceResult<JobReviewQueuePage>{
                ErrorCode::kInvalidParam,
                {}};
        }

        if (page < 1 ||
            pageSize < 1 ||
            pageSize > kMaxQueuePageSize)
        {
            return ServiceResult<JobReviewQueuePage>{
                ErrorCode::kInvalidParam,
                {}};
        }

        try
        {
            JobReviewQueuePage result;
            result.page = page;
            result.pageSize = pageSize;
            result.total = dao_.CountQueue(reviewStatus);
            result.items = dao_.ListQueue(
                reviewStatus,
                pageSize,
                (page - 1) * pageSize);

            return ServiceResult<JobReviewQueuePage>{
                ErrorCode::kSuccess,
                std::move(result)};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "review queue read failed: {}",
                error.what());

            return ServiceResult<JobReviewQueuePage>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

    ServiceResult<JobReviewResult> JobReviewService::Review(
        std::uint64_t jobId,
        const std::string &decision,
        const std::string &reviewer,
        const std::string &note,
        const JobReviewFix &fix)
    {
        JobReviewDecision parsed = JobReviewDecision::kApprove;

        if (jobId == 0 ||
            !ParseJobReviewDecision(decision, &parsed))
        {
            return ServiceResult<JobReviewResult>{
                ErrorCode::kInvalidParam,
                {}};
        }

        if (reviewer.empty())
        {
            return ServiceResult<JobReviewResult>{
                ErrorCode::kInvalidParam,
                {}};
        }

        if (parsed == JobReviewDecision::kReject &&
            note.empty())
        {
            return ServiceResult<JobReviewResult>{
                ErrorCode::kInvalidParam,
                {}};
        }

        if (parsed == JobReviewDecision::kFix)
        {
            std::string code;
            std::string field;
            std::string reason;

            if (!ValidateFix(
                    fix,
                    platforms_,
                    &code,
                    &field,
                    &reason))
            {
                JobReviewResult rejected;
                rejected.jobId = jobId;
                rejected.decision = ToString(parsed);
                rejected.reviewer = reviewer;
                rejected.note = note;
                rejected.diagnosticCode = code;
                rejected.diagnosticField = field;
                rejected.diagnosticReason = reason;

                return ServiceResult<JobReviewResult>{
                    ErrorCode::kInvalidParam,
                    std::move(rejected)};
            }
        }

        try
        {
            const std::optional<JobReviewState> state =
                dao_.FindState(jobId);

            if (!state.has_value())
            {
                return ServiceResult<JobReviewResult>{
                    ErrorCode::kJobNotFound,
                    {}};
            }

            const std::string resolution =
                ResolutionFor(parsed);
            const std::int32_t target =
                parsed == JobReviewDecision::kReject ? 2 : 0;

            JobReviewResult result;
            result.jobId = jobId;
            result.reviewStatus = target;
            result.decision = ToString(parsed);
            result.reviewer = reviewer;
            result.note = note;

            // Repeating the identical decision is an idempotent 200 no-op.
            if (state->lastResolution == resolution &&
                state->reviewStatus == target)
            {
                result.idempotent = true;
                return ServiceResult<JobReviewResult>{
                    ErrorCode::kSuccess,
                    std::move(result)};
            }

            // Any other transition away from the pending state is a conflict.
            if (state->reviewStatus != 1)
            {
                return ServiceResult<JobReviewResult>{
                    ErrorCode::kInvalidStatusTransition,
                    std::move(result)};
            }

            std::vector<JobReviewAuditField> audit;

            if (parsed == JobReviewDecision::kFix)
            {
                if (fix.company.has_value())
                {
                    audit.push_back(JobReviewAuditField{
                        "company",
                        state->company,
                        Trim(fix.company.value())});
                }

                if (fix.title.has_value())
                {
                    audit.push_back(JobReviewAuditField{
                        "title",
                        state->title,
                        Trim(fix.title.value())});
                }

                if (fix.sourceUrl.has_value())
                {
                    audit.push_back(JobReviewAuditField{
                        "source_url",
                        state->sourceUrl,
                        Trim(fix.sourceUrl.value())});
                }

                if (fix.description.has_value())
                {
                    audit.push_back(JobReviewAuditField{
                        "description",
                        state->description,
                        fix.description.value()});
                }

                for (const JobReviewAuditField &field : audit)
                {
                    result.changedFields.push_back(field.field);
                }
            }

            dao_.ApplyDecision(
                jobId,
                parsed,
                reviewer,
                note,
                fix,
                audit);

            return ServiceResult<JobReviewResult>{
                ErrorCode::kSuccess,
                std::move(result)};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "review decision failed: {}",
                error.what());

            return ServiceResult<JobReviewResult>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

    ServiceResult<JobReviewBackfillReport> JobReviewService::Backfill(
        const std::string &mode,
        const std::string &actor)
    {
        if (mode != "dry_run" &&
            mode != "apply" &&
            mode != "revert")
        {
            return ServiceResult<JobReviewBackfillReport>{
                ErrorCode::kInvalidParam,
                {}};
        }

        try
        {
            JobReviewBackfillReport report;
            report.mode = mode;
            report.activeJobs = dao_.CountActiveJobs();
            report.beforeNeedsReview = dao_.CountByReviewStatus(1);

            if (mode == "revert")
            {
                report.reverted = dao_.RevertBackfill();
                report.afterNeedsReview =
                    dao_.CountByReviewStatus(1);
                report.wouldFlag = 0;

                return ServiceResult<JobReviewBackfillReport>{
                    ErrorCode::kSuccess,
                    std::move(report)};
            }

            const std::vector<JobReviewCandidate> candidates =
                dao_.ListActiveCandidates(kBackfillCandidateLimit);

            report.scanned = candidates.size();

            std::vector<JobImportData> jobData;
            jobData.reserve(candidates.size());

            for (const JobReviewCandidate &candidate : candidates)
            {
                jobData.push_back(ToJobImportData(candidate));
            }

            // The SAME deterministic engine as ingest: no duplicated SQL
            // heuristics, no second source of truth (ExecPlan 0015A 5.4.2).
            std::vector<JobQualityReport> reports;
            reports.reserve(jobData.size());

            for (const JobImportData &job : jobData)
            {
                reports.push_back(
                    EvaluateJobQuality(job, platforms_));
            }

            MarkDuplicateContentInBatch(jobData, &reports);

            std::vector<JobReviewBackfillEntry> flagged;

            for (std::size_t index = 0;
                 index < candidates.size() &&
                 index < reports.size();
                 ++index)
            {
                const JobQualityReport &quality = reports[index];

                if (quality.reviewStatus != 1)
                {
                    continue;
                }

                JobReviewBackfillEntry entry;
                entry.jobId = candidates[index].jobId;

                for (const JobQualityFinding &finding :
                     quality.findings)
                {
                    if (finding.outcome !=
                        JobQualityOutcome::kNeedsReview)
                    {
                        continue;
                    }

                    ++report.perReason[finding.code];
                    entry.findings.push_back(finding);

                    if (report.sample.size() <
                            kBackfillSampleLimit &&
                        entry.findings.size() == 1)
                    {
                        JobReviewQueueItem sample;
                        sample.jobId = entry.jobId;
                        sample.company = candidates[index].company;
                        sample.title = candidates[index].title;
                        sample.sourceUrl =
                            candidates[index].sourceUrl;
                        sample.reviewStatus = 1;
                        sample.reasonCode = finding.code;
                        sample.reasonText = finding.reason;
                        sample.outcome =
                            ToString(finding.outcome);
                        report.sample.push_back(
                            std::move(sample));
                    }
                }

                if (!entry.findings.empty())
                {
                    flagged.push_back(std::move(entry));
                }
            }

            report.wouldFlag = flagged.size();
            report.hiddenSharePercent =
                report.activeJobs > 0
                    ? (report.wouldFlag * 100) /
                          report.activeJobs
                    : 0;
            report.thresholdExceeded =
                report.wouldFlag > report.thresholdRows ||
                (report.activeJobs > 0 &&
                 report.wouldFlag * 100 >
                     report.thresholdPercent *
                         report.activeJobs);

            if (mode == "dry_run")
            {
                // Projection only; nothing is written.
                report.afterNeedsReview =
                    report.beforeNeedsReview +
                    report.wouldFlag;

                return ServiceResult<JobReviewBackfillReport>{
                    ErrorCode::kSuccess,
                    std::move(report)};
            }

            dao_.ApplyBackfill(flagged, actor);

            report.applied = flagged.size();
            report.afterNeedsReview =
                dao_.CountByReviewStatus(1);

            return ServiceResult<JobReviewBackfillReport>{
                ErrorCode::kSuccess,
                std::move(report)};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "review backfill failed: {}",
                error.what());

            return ServiceResult<JobReviewBackfillReport>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

} // namespace jobpilot::service
