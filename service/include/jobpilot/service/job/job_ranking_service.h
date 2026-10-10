#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "jobpilot/service/auth/session_authenticator.h"
#include "jobpilot/service/job/job_ranking.h"
#include "jobpilot/service/job/job_ranking_dao_interface.h"
#include "jobpilot/service/result.h"
#include "jobpilot/service/user/user_dao_interface.h"

namespace jobpilot::service
{
    struct RankedJobPage
    {
        std::vector<RankedJob> list;
        std::size_t total = 0;
        std::size_t rankedTotal = 0;
        bool truncated = false;
        std::size_t page = 1;
        std::size_t pageSize = 20;
    };

    // 0015 B3: strict list page. `total` counts the strict-FILTERED set (the
    // rows a caller can actually walk), and `truncated` states whether the
    // candidate window cap hid unexamined candidates.
    struct ExplainedListPage
    {
        std::vector<RankedJob> list;
        std::size_t total = 0;
        std::size_t examined = 0;
        bool truncated = false;
        std::size_t page = 1;
        std::size_t pageSize = 20;
    };

    class JobRankingService
    {
    public:
        JobRankingService(
            IJobRankingDao &jobRankingDao,
            IUserProfileReader &profileReader,
            ISessionAuthenticator &authenticator);

        ServiceResult<std::uint64_t> Authenticate(
            const std::string &token);

        ServiceResult<RankedJobPage> RankJobs(
            std::uint64_t userId,
            const JobSearchFilter &filter,
            std::size_t page,
            std::size_t pageSize);

        // 0015 B4: deterministic explanation for an already-paged list of rows
        // (the public list keeps its own ordering/total; only the match keys are
        // added). Never excludes and never re-orders. An absent/partial profile
        // is allowed: the missing side becomes an advisory reason, not a 400.
        ServiceResult<std::vector<RankedJob>> ExplainJobs(
            std::uint64_t userId,
            const JobSearchFilter &filter,
            const std::vector<JobSummary> &jobs);

        // 0015 B3: the strict (`strict=true`) list page. Candidate window in the
        // LIST's ordering -> deal-breaker exclusion -> page. `total` is the
        // strict-filtered count; `truncated` marks a capped window.
        ServiceResult<ExplainedListPage> ExplainStrictList(
            std::uint64_t userId,
            const JobSearchFilter &filter,
            std::size_t page,
            std::size_t pageSize);

    private:
        IJobRankingDao &jobRankingDao_;
        IUserProfileReader &profileReader_;
        ISessionAuthenticator &authenticator_;
    };

} // namespace jobpilot::service
