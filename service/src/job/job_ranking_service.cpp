#include "jobpilot/service/job/job_ranking_service.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "jobpilot/core/logger.h"

namespace jobpilot::service
{
    namespace
    {
        constexpr std::size_t kMaxPageSize = 100;
        constexpr std::size_t kMaxRankingCandidates = 2000;
    } // namespace

    JobRankingService::JobRankingService(
        IJobRankingDao &jobRankingDao,
        IUserProfileReader &profileReader,
        ISessionAuthenticator &authenticator)
        : jobRankingDao_(jobRankingDao),
          profileReader_(profileReader),
          authenticator_(authenticator)
    {
    }

    ServiceResult<std::uint64_t>
    JobRankingService::Authenticate(
        const std::string &token)
    {
        return authenticator_.Authenticate(token);
    }

    ServiceResult<RankedJobPage>
    JobRankingService::RankJobs(
        std::uint64_t userId,
        const JobSearchFilter &filter,
        std::size_t page,
        std::size_t pageSize)
    {
        if (userId == 0)
        {
            return ServiceResult<RankedJobPage>{
                ErrorCode::kUnauthorized,
                {}};
        }

        if (page == 0 ||
            pageSize == 0 ||
            pageSize > kMaxPageSize)
        {
            return ServiceResult<RankedJobPage>{
                ErrorCode::kInvalidParam,
                {}};
        }

        JobSearchFilter normalized;

        if (!NormalizeJobSearchFilter(
                filter,
                &normalized))
        {
            return ServiceResult<RankedJobPage>{
                ErrorCode::kInvalidParam,
                {}};
        }

        const std::size_t pageIndex = page - 1;

        if (pageIndex >
            std::numeric_limits<std::size_t>::max() /
                pageSize)
        {
            return ServiceResult<RankedJobPage>{
                ErrorCode::kInvalidParam,
                {}};
        }

        try
        {
            const std::optional<UserProfile> profile =
                profileReader_.FindProfile(userId);
            const std::vector<UserSkill> skills =
                profileReader_.ListSkills(userId);

            if (!profile.has_value() &&
                skills.empty())
            {
                // A new account without a profile or skills is a normal
                // state, not a bad request. Return an empty page so the UI can
                // render its onboarding state without a spurious 400.
                RankedJobPage empty;
                empty.page = page;
                empty.pageSize = pageSize;
                return ServiceResult<RankedJobPage>{
                    ErrorCode::kSuccess,
                    empty};
            }

            RankedJobPage result;
            result.page = page;
            result.pageSize = pageSize;
            result.total =
                jobRankingDao_.CountRankingCandidates(
                    normalized);

            result.list.clear();
            const std::size_t candidateLimit =
                std::min(
                    result.total,
                    kMaxRankingCandidates);
            const std::vector<JobSummary> candidates =
                jobRankingDao_.SearchRankingCandidates(
                    normalized,
                    candidateLimit);

            std::vector<std::uint64_t> jobIds;
            jobIds.reserve(candidates.size());

            for (const JobSummary &candidate : candidates)
            {
                jobIds.push_back(candidate.id);
            }

            const std::vector<JobRequirementsRecord>
                requirementRecords =
                    jobRankingDao_.ListRequirementsForJobs(
                        jobIds);

            std::unordered_map<
                std::uint64_t,
                std::vector<JobRequirement>>
                requirementsByJob;

            for (const JobRequirementsRecord &record :
                 requirementRecords)
            {
                requirementsByJob.emplace(
                    record.jobId,
                    record.requirements);
            }

            std::vector<RankedJob> ranked;
            ranked.reserve(candidates.size());
            const auto now =
                std::chrono::system_clock::now();

            for (const JobSummary &candidate : candidates)
            {
                const auto requirements =
                    requirementsByJob.find(
                        candidate.id);
                const std::vector<JobRequirement> empty;
                const std::vector<JobRequirement> &jobRequirements =
                    requirements != requirementsByJob.end()
                        ? requirements->second
                        : empty;

                ranked.push_back(
                    ScoreJob(
                        candidate,
                        jobRequirements,
                        profile,
                        skills,
                        normalized,
                        now));

                // 0015 B1: deterministic explanation; the weighted score above
                // is untouched (decision 5).
                RankedJob &scored = ranked.back();
                scored.reasons =
                    ProjectMatchReasons(
                        candidate,
                        profile,
                        scored.matchedSkills,
                        scored.missingSkills,
                        now);
                scored.dealBreakers =
                    EvaluateMatchDealBreakers(
                        candidate,
                        profile,
                        now);
                scored.deadlineUrgency =
                    ClassifyDeadlineUrgency(
                        candidate.deadline,
                        candidate.deadlineDateOnly,
                        now);
            }

            std::sort(
                ranked.begin(),
                ranked.end(),
                [](const RankedJob &left,
                   const RankedJob &right)
                {
                    if (left.score != right.score)
                    {
                        return left.score > right.score;
                    }

                    if (left.job.createTime !=
                        right.job.createTime)
                    {
                        return left.job.createTime >
                               right.job.createTime;
                    }

                    return left.job.id > right.job.id;
                });

            result.rankedTotal = ranked.size();
            result.truncated =
                result.total > result.rankedTotal;
            const std::size_t offset =
                pageIndex * pageSize;

            if (offset < ranked.size())
            {
                const std::size_t end =
                    std::min(
                        ranked.size(),
                        offset + pageSize);
                result.list.assign(
                    ranked.begin() +
                        static_cast<std::ptrdiff_t>(offset),
                    ranked.begin() +
                        static_cast<std::ptrdiff_t>(end));
            }

            return ServiceResult<RankedJobPage>{
                ErrorCode::kSuccess,
                std::move(result)};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "job ranking failed: {}",
                error.what());

            return ServiceResult<RankedJobPage>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

    ServiceResult<std::vector<RankedJob>>
    JobRankingService::ExplainJobs(
        std::uint64_t userId,
        const JobSearchFilter &filter,
        const std::vector<JobSummary> &jobs)
    {
        if (userId == 0)
        {
            return ServiceResult<std::vector<RankedJob>>{
                ErrorCode::kUnauthorized,
                {}};
        }

        JobSearchFilter normalized;

        if (!NormalizeJobSearchFilter(
                filter,
                &normalized))
        {
            return ServiceResult<std::vector<RankedJob>>{
                ErrorCode::kInvalidParam,
                {}};
        }

        try
        {
            const std::optional<UserProfile> profile =
                profileReader_.FindProfile(userId);
            const std::vector<UserSkill> skills =
                profileReader_.ListSkills(userId);

            std::vector<std::uint64_t> jobIds;
            jobIds.reserve(jobs.size());

            for (const JobSummary &job : jobs)
            {
                jobIds.push_back(job.id);
            }

            const std::vector<JobRequirementsRecord>
                requirementRecords =
                    jobRankingDao_.ListRequirementsForJobs(jobIds);

            std::unordered_map<
                std::uint64_t,
                std::vector<JobRequirement>>
                requirementsByJob;

            for (const JobRequirementsRecord &record :
                 requirementRecords)
            {
                requirementsByJob.emplace(
                    record.jobId,
                    record.requirements);
            }

            const auto now =
                std::chrono::system_clock::now();

            std::vector<RankedJob> explained;
            explained.reserve(jobs.size());

            for (const JobSummary &job : jobs)
            {
                const auto requirements =
                    requirementsByJob.find(job.id);
                const std::vector<JobRequirement> empty;
                const std::vector<JobRequirement> &jobRequirements =
                    requirements != requirementsByJob.end()
                        ? requirements->second
                        : empty;

                explained.push_back(
                    ScoreJob(
                        job,
                        jobRequirements,
                        profile,
                        skills,
                        normalized,
                        now));

                RankedJob &scored = explained.back();
                scored.reasons =
                    ProjectMatchReasons(
                        job,
                        profile,
                        scored.matchedSkills,
                        scored.missingSkills,
                        now);
                scored.dealBreakers =
                    EvaluateMatchDealBreakers(
                        job,
                        profile,
                        now);
                scored.deadlineUrgency =
                    ClassifyDeadlineUrgency(
                        job.deadline,
                        job.deadlineDateOnly,
                        now);
            }

            return ServiceResult<std::vector<RankedJob>>{
                ErrorCode::kSuccess,
                std::move(explained)};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "job explanation failed: {}",
                error.what());

            return ServiceResult<std::vector<RankedJob>>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

    ServiceResult<ExplainedListPage>
    JobRankingService::ExplainStrictList(
        std::uint64_t userId,
        const JobSearchFilter &filter,
        std::size_t page,
        std::size_t pageSize)
    {
        if (userId == 0)
        {
            return ServiceResult<ExplainedListPage>{
                ErrorCode::kUnauthorized,
                {}};
        }

        if (page == 0 ||
            pageSize == 0 ||
            pageSize > kMaxPageSize)
        {
            return ServiceResult<ExplainedListPage>{
                ErrorCode::kInvalidParam,
                {}};
        }

        JobSearchFilter normalized;

        if (!NormalizeJobSearchFilter(
                filter,
                &normalized))
        {
            return ServiceResult<ExplainedListPage>{
                ErrorCode::kInvalidParam,
                {}};
        }

        const std::size_t pageIndex = page - 1;

        if (pageIndex >
            std::numeric_limits<std::size_t>::max() /
                pageSize)
        {
            return ServiceResult<ExplainedListPage>{
                ErrorCode::kInvalidParam,
                {}};
        }

        try
        {
            const std::optional<UserProfile> profile =
                profileReader_.FindProfile(userId);
            const std::vector<UserSkill> skills =
                profileReader_.ListSkills(userId);

            const std::size_t matching =
                jobRankingDao_.CountRankingCandidates(normalized);
            const std::size_t window = std::min(
                matching,
                kMaxRankingCandidates);
            const std::vector<JobSummary> candidates =
                jobRankingDao_.SearchListWindow(
                    normalized,
                    window);

            std::vector<std::uint64_t> candidateIds;
            candidateIds.reserve(candidates.size());

            for (const JobSummary &candidate : candidates)
            {
                candidateIds.push_back(candidate.id);
            }

            const std::vector<JobRequirementsRecord>
                requirementRecords =
                    jobRankingDao_.ListRequirementsForJobs(
                        candidateIds);

            std::unordered_map<
                std::uint64_t,
                std::vector<JobRequirement>>
                requirementsByJob;

            for (const JobRequirementsRecord &record :
                 requirementRecords)
            {
                requirementsByJob.emplace(
                    record.jobId,
                    record.requirements);
            }

            const auto now =
                std::chrono::system_clock::now();

            std::vector<RankedJob> survivors;
            survivors.reserve(candidates.size());

            for (const JobSummary &candidate : candidates)
            {
                if (!EvaluateMatchDealBreakers(
                         candidate,
                         profile,
                         now)
                         .empty())
                {
                    // `strict=true`: an evidence-backed veto excludes the row.
                    continue;
                }

                const auto requirements =
                    requirementsByJob.find(candidate.id);
                const std::vector<JobRequirement> empty;
                const std::vector<JobRequirement> &jobRequirements =
                    requirements != requirementsByJob.end()
                        ? requirements->second
                        : empty;

                survivors.push_back(
                    ScoreJob(
                        candidate,
                        jobRequirements,
                        profile,
                        skills,
                        normalized,
                        now));

                RankedJob &scored = survivors.back();
                scored.reasons =
                    ProjectMatchReasons(
                        candidate,
                        profile,
                        scored.matchedSkills,
                        scored.missingSkills,
                        now);
                scored.dealBreakers.clear();
                scored.deadlineUrgency =
                    ClassifyDeadlineUrgency(
                        candidate.deadline,
                        candidate.deadlineDateOnly,
                        now);
            }

            ExplainedListPage result;
            result.page = page;
            result.pageSize = pageSize;
            result.examined = candidates.size();
            result.total = survivors.size();
            // The window cap hid candidates we could not examine: say so rather
            // than returning a total the caller cannot walk.
            result.truncated = matching > window;

            const std::size_t offset = pageIndex * pageSize;

            if (offset < survivors.size())
            {
                const std::size_t end = std::min(
                    survivors.size(),
                    offset + pageSize);
                result.list.assign(
                    survivors.begin() +
                        static_cast<std::ptrdiff_t>(offset),
                    survivors.begin() +
                        static_cast<std::ptrdiff_t>(end));
            }

            return ServiceResult<ExplainedListPage>{
                ErrorCode::kSuccess,
                std::move(result)};
        }
        catch (const std::exception &error)
        {
            jobpilot::core::Logger::Get()->error(
                "strict list explanation failed: {}",
                error.what());

            return ServiceResult<ExplainedListPage>{
                ErrorCode::kDatabaseError,
                {}};
        }
    }

} // namespace jobpilot::service
