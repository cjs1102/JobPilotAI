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
                return ServiceResult<RankedJobPage>{
                    ErrorCode::kInvalidParam,
                    {}};
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

} // namespace jobpilot::service
