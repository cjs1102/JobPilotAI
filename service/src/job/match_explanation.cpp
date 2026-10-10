#include "jobpilot/service/job/match_explanation.h"

#include <ctime>
#include <iomanip>
#include <sstream>

#include "jobpilot/service/ai/match_rule.h"

namespace jobpilot::service
{
    namespace
    {
        constexpr std::int64_t kSecondsPerDay = 24 * 60 * 60;
        constexpr std::int64_t kSecondsPerHour = 60 * 60;

        // `deadline` is the stored UTC instant (docs/03 section 9.1). timegm
        // keeps the comparison in UTC instead of the process timezone.
        std::optional<std::chrono::system_clock::time_point> ParseUtc(
            const std::string &value)
        {
            std::tm time{};
            std::istringstream input(value);
            input >> std::get_time(
                &time,
                "%Y-%m-%d %H:%M:%S");

            if (input.fail())
            {
                return std::nullopt;
            }

            const std::time_t parsed = timegm(&time);

            if (parsed == static_cast<std::time_t>(-1))
            {
                return std::nullopt;
            }

            return std::chrono::system_clock::from_time_t(parsed);
        }

        const char *EducationName(
            std::uint32_t level)
        {
            switch (level)
            {
            case 1:
                return "不限";
            case 2:
                return "大专";
            case 3:
                return "本科";
            case 4:
                return "硕士";
            case 5:
                return "博士";
            default:
                return "未知";
            }
        }

        const char *CampusName(
            std::uint32_t status)
        {
            switch (status)
            {
            case 1:
                return "非应届";
            case 2:
                return "应届";
            case 3:
                return "在校";
            default:
                return "未知";
            }
        }

        const char *NatureName(
            std::uint32_t nature)
        {
            switch (nature)
            {
            case 1:
                return "校招";
            case 2:
                return "社招";
            case 3:
                return "实习";
            default:
                return "未知";
            }
        }

        std::string JoinSkills(
            const std::vector<std::string> &skills)
        {
            std::string joined;

            for (const std::string &skill : skills)
            {
                if (!joined.empty())
                {
                    joined += "、";
                }

                joined += skill;
            }

            return joined;
        }
    } // namespace

    const char *ToString(DeadlineUrgency urgency)
    {
        switch (urgency)
        {
        case DeadlineUrgency::kExpired:
            return "expired";
        case DeadlineUrgency::kUrgent:
            return "urgent";
        case DeadlineUrgency::kSoon:
            return "soon";
        case DeadlineUrgency::kNormal:
            return "normal";
        case DeadlineUrgency::kUnknown:
            return "unknown";
        }

        return "unknown";
    }

    DeadlineUrgency ClassifyDeadlineUrgency(
        const std::optional<std::string> &deadline,
        const std::optional<bool> &deadlineDateOnly,
        std::chrono::system_clock::time_point now)
    {
        if (!deadline.has_value())
        {
            // A NULL deadline is unknown and is NEVER expired (F13).
            return DeadlineUrgency::kUnknown;
        }

        const auto parsed = ParseUtc(deadline.value());

        if (!parsed.has_value())
        {
            return DeadlineUrgency::kUnknown;
        }

        std::chrono::system_clock::time_point instant =
            parsed.value();

        if (deadlineDateOnly.value_or(false))
        {
            // A date-only deadline is stored at Beijing noon (docs/03 section
            // 9.1); it expires at the end of that Beijing day.
            instant += std::chrono::hours(12);
        }

        if (now >= instant)
        {
            return DeadlineUrgency::kExpired;
        }

        const std::int64_t remainingSeconds =
            std::chrono::duration_cast<std::chrono::seconds>(
                instant - now)
                .count();

        if (remainingSeconds <=
            kDeadlineUrgentDays * kSecondsPerDay)
        {
            return DeadlineUrgency::kUrgent;
        }

        if (remainingSeconds <=
            kDeadlineSoonDays * kSecondsPerDay)
        {
            return DeadlineUrgency::kSoon;
        }

        return DeadlineUrgency::kNormal;
    }

    std::vector<MatchDealBreaker> EvaluateMatchDealBreakers(
        const JobSummary &job,
        const std::optional<UserProfile> &profile,
        std::chrono::system_clock::time_point now)
    {
        std::vector<MatchDealBreaker> breakers;

        const bool hasProfile = profile.has_value();

        // Education: only when BOTH sides state a level. A NULL on either side
        // is never a veto (the missing side becomes an advisory reason).
        if (job.educationRequired.has_value() &&
            hasProfile &&
            profile->educationLevel.has_value())
        {
            const std::uint32_t required =
                job.educationRequired.value();
            const std::uint32_t actual =
                profile->educationLevel.value();

            if (required >= 2 && required <= 5 &&
                actual >= 1 && actual <= 5 &&
                actual < required)
            {
                breakers.push_back(
                    MatchDealBreaker{
                        kDealBreakerEducationUnmet,
                        std::string("岗位要求") +
                            EducationName(required) +
                            "及以上，画像学历为" +
                            EducationName(actual)});
            }
        }

        // Campus status vs job nature: the narrow documented matrix.
        if (job.jobNature.has_value() &&
            hasProfile &&
            profile->campusStatus.has_value())
        {
            const std::uint32_t nature =
                job.jobNature.value();
            const std::uint32_t campus =
                profile->campusStatus.value();
            const bool incompatible =
                (campus == 1 && (nature == 1 || nature == 3)) ||
                (campus == 3 && nature == 2);

            if (incompatible)
            {
                breakers.push_back(
                    MatchDealBreaker{
                        kDealBreakerCampusIncompatible,
                        std::string("画像为") +
                            CampusName(campus) +
                            "，岗位性质为" +
                            NatureName(nature)});
            }
        }

        // Salary: only when both ranges are complete and disjoint.
        if (job.salaryMin.has_value() &&
            job.salaryMax.has_value() &&
            hasProfile &&
            profile->salaryMin.has_value() &&
            profile->salaryMax.has_value())
        {
            const std::uint32_t jobMin =
                job.salaryMin.value();
            const std::uint32_t jobMax =
                job.salaryMax.value();
            const std::uint32_t profileMin =
                profile->salaryMin.value();
            const std::uint32_t profileMax =
                profile->salaryMax.value();

            if (jobMin > profileMax || profileMin > jobMax)
            {
                breakers.push_back(
                    MatchDealBreaker{
                        kDealBreakerSalaryNoIntersection,
                        "岗位薪资与期望薪资无交集"});
            }
        }

        if (ClassifyDeadlineUrgency(
                job.deadline,
                job.deadlineDateOnly,
                now) == DeadlineUrgency::kExpired)
        {
            breakers.push_back(
                MatchDealBreaker{
                    kDealBreakerDeadlineExpired,
                    "投递截止时间已过"});
        }

        return breakers;
    }

    std::vector<MatchReason> ProjectMatchReasons(
        const JobSummary &job,
        const std::optional<UserProfile> &profile,
        const std::vector<std::string> &matchedSkills,
        const std::vector<std::string> &missingSkills,
        std::chrono::system_clock::time_point now)
    {
        std::vector<MatchReason> reasons;

        if (!matchedSkills.empty())
        {
            reasons.push_back(
                MatchReason{
                    "skills_matched",
                    "匹配技能：" + JoinSkills(matchedSkills),
                    kEvidenceSkills});
        }

        if (!missingSkills.empty())
        {
            reasons.push_back(
                MatchReason{
                    "skills_missing",
                    "缺少技能：" + JoinSkills(missingSkills),
                    kEvidenceSkills});
        }

        if (profile.has_value())
        {
            // NULL profile fields are advisories, never penalties.
            if (!profile->educationLevel.has_value() &&
                job.educationRequired.has_value())
            {
                reasons.push_back(
                    MatchReason{
                        "education_unknown",
                        "岗位有学历要求，画像未填写学历；不参与排除",
                        kEvidenceProfile});
            }

            if (!profile->campusStatus.has_value() &&
                job.jobNature.has_value())
            {
                reasons.push_back(
                    MatchReason{
                        "campus_unknown",
                        "岗位有性质要求，画像未填写应届状态；不参与排除",
                        kEvidenceProfile});
            }

            if ((!profile->salaryMin.has_value() ||
                 !profile->salaryMax.has_value()) &&
                (job.salaryMin.has_value() ||
                 job.salaryMax.has_value()))
            {
                reasons.push_back(
                    MatchReason{
                        "salary_unknown",
                        "画像未填写完整薪资期望，薪资区间未参与比较",
                        kEvidenceProfile});
            }

            if (profile->educationLevel.has_value() &&
                job.educationRequired.has_value())
            {
                const std::uint32_t required =
                    job.educationRequired.value();
                const std::uint32_t actual =
                    profile->educationLevel.value();

                if (required >= 2 && required <= 5 &&
                    actual >= 1 && actual <= 5)
                {
                    reasons.push_back(
                        MatchReason{
                            actual >= required
                                ? "education_met"
                                : "education_unmet",
                            std::string("岗位要求") +
                                EducationName(required) +
                                "，画像为" +
                                EducationName(actual),
                            kEvidenceJobRequirements});
                }
            }

            if (profile->location.has_value() &&
                job.location.has_value())
            {
                const bool same =
                    profile->location.value() ==
                    job.location.value();

                reasons.push_back(
                    MatchReason{
                        same
                            ? "location_match"
                            : "location_mismatch",
                        std::string("岗位地点") +
                            job.location.value() +
                            "，期望城市" +
                            profile->location.value(),
                        kEvidenceProfile});
            }
        }

        switch (ClassifyDeadlineUrgency(
            job.deadline,
            job.deadlineDateOnly,
            now))
        {
        case DeadlineUrgency::kExpired:
            reasons.push_back(
                MatchReason{
                    "deadline_expired",
                    "投递截止时间已过",
                    kEvidenceDeadline});
            break;
        case DeadlineUrgency::kUrgent:
            reasons.push_back(
                MatchReason{
                    "deadline_urgent",
                    "投递截止时间在 7 天内",
                    kEvidenceDeadline});
            break;
        case DeadlineUrgency::kSoon:
            reasons.push_back(
                MatchReason{
                    "deadline_soon",
                    "投递截止时间在 30 天内",
                    kEvidenceDeadline});
            break;
        case DeadlineUrgency::kNormal:
            reasons.push_back(
                MatchReason{
                    "deadline_normal",
                    "投递截止时间充裕",
                    kEvidenceDeadline});
            break;
        case DeadlineUrgency::kUnknown:
            // No deadline delivered: stated as unknown, never as expired.
            break;
        }

        return reasons;
    }
} // namespace jobpilot::service
