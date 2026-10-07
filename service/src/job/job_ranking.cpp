#include "jobpilot/service/job/job_ranking.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace jobpilot::service
{
    namespace
    {
        constexpr double kSkillWeight = 0.55;
        constexpr double kTitleWeight = 0.15;
        constexpr double kSalaryWeight = 0.15;
        constexpr double kLocationWeight = 0.10;
        constexpr double kFirstSeenWeight = 0.05;
        constexpr double kFirstSeenWindowDays = 30.0;

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

        std::string Trim(
            const std::string &value)
        {
            std::size_t begin = 0;
            std::size_t end = value.size();

            while (begin < end &&
                   std::isspace(
                       static_cast<unsigned char>(
                           value[begin])))
            {
                ++begin;
            }

            while (end > begin &&
                   std::isspace(
                       static_cast<unsigned char>(
                           value[end - 1])))
            {
                --end;
            }

            return value.substr(
                begin,
                end - begin);
        }

        std::vector<std::string> Tokenize(
            const std::string &value)
        {
            std::vector<std::string> tokens;
            std::string current;

            for (unsigned char character : value)
            {
                if (std::isalnum(character))
                {
                    current.push_back(
                        static_cast<char>(
                            std::tolower(character)));
                    continue;
                }

                if (!current.empty())
                {
                    tokens.push_back(
                        std::move(current));
                    current.clear();
                }
            }

            if (!current.empty())
            {
                tokens.push_back(
                    std::move(current));
            }

            return tokens;
        }

        std::optional<double> SkillFit(
            const std::vector<JobRequirement> &requirements,
            const std::unordered_map<
                std::string,
                std::int32_t> &skillLevels,
            std::vector<std::string> *matchedSkills,
            std::vector<std::string> *missingSkills)
        {
            if (requirements.empty())
            {
                return std::nullopt;
            }

            double weightedMatch = 0.0;
            double totalWeight = 0.0;

            for (const JobRequirement &requirement :
                 requirements)
            {
                const std::int32_t requiredLevel =
                    std::max(1, requirement.level);
                totalWeight += requiredLevel;

                const auto found =
                    skillLevels.find(
                        FoldAsciiCase(
                            Trim(requirement.skill)));

                if (found == skillLevels.end())
                {
                    missingSkills->push_back(
                        requirement.skill);
                    continue;
                }

                matchedSkills->push_back(
                    requirement.skill);
                weightedMatch +=
                    std::min(
                        found->second,
                        requiredLevel) *
                    requiredLevel;
            }

            if (totalWeight <= 0.0)
            {
                return std::nullopt;
            }

            return weightedMatch / totalWeight;
        }

        std::optional<double> TitleFit(
            const std::optional<UserProfile> &profile,
            const std::string &title)
        {
            if (!profile.has_value() ||
                !profile->targetPosition.has_value())
            {
                return std::nullopt;
            }

            const std::vector<std::string> targetTokens =
                Tokenize(
                    profile->targetPosition.value());

            if (targetTokens.empty())
            {
                return std::nullopt;
            }

            const std::vector<std::string> titleTokens =
                Tokenize(title);
            const std::unordered_set<std::string> titleSet(
                titleTokens.begin(),
                titleTokens.end());

            std::size_t matches = 0;

            for (const std::string &token :
                 targetTokens)
            {
                if (titleSet.find(token) !=
                    titleSet.end())
                {
                    ++matches;
                }
            }

            return static_cast<double>(matches) /
                   static_cast<double>(
                       targetTokens.size());
        }

        std::optional<double> SalaryFit(
            const std::optional<UserProfile> &profile,
            const JobSummary &job)
        {
            if (!profile.has_value() ||
                !profile->salaryMin.has_value() ||
                !profile->salaryMax.has_value() ||
                !job.salaryMin.has_value() ||
                !job.salaryMax.has_value())
            {
                return std::nullopt;
            }

            const std::uint64_t profileMin =
                profile->salaryMin.value();
            const std::uint64_t profileMax =
                profile->salaryMax.value();
            const std::uint64_t jobMin =
                job.salaryMin.value();
            const std::uint64_t jobMax =
                job.salaryMax.value();

            const std::uint64_t overlapStart =
                std::max(profileMin, jobMin);
            const std::uint64_t overlapEnd =
                std::min(profileMax, jobMax);

            if (overlapEnd < overlapStart)
            {
                return 0.0;
            }

            const std::uint64_t overlap =
                overlapEnd - overlapStart;
            const std::uint64_t profileSpan =
                profileMax - profileMin;
            const std::uint64_t jobSpan =
                jobMax - jobMin;
            const std::uint64_t denominator =
                std::max<std::uint64_t>(
                    1,
                    std::max(
                        profileSpan,
                        jobSpan));

            return std::min(
                1.0,
                static_cast<double>(overlap) /
                    static_cast<double>(denominator));
        }

        std::optional<double> LocationFit(
            const std::optional<UserProfile> &profile,
            const JobSummary &job)
        {
            if (!profile.has_value() ||
                !profile->location.has_value() ||
                !job.location.has_value())
            {
                return std::nullopt;
            }

            return FoldAsciiCase(
                       Trim(
                           profile->location.value())) ==
                           FoldAsciiCase(
                               Trim(
                                   job.location.value()))
                       ? 1.0
                       : 0.0;
        }

        std::optional<std::time_t> ParseTime(
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

            const std::time_t parsed =
                std::mktime(&time);

            if (parsed == static_cast<std::time_t>(-1))
            {
                return std::nullopt;
            }

            return parsed;
        }

        std::optional<double> FirstSeenFit(
            const JobSummary &job,
            std::chrono::system_clock::time_point now)
        {
            const std::optional<std::time_t> created =
                ParseTime(job.createTime);

            if (!created.has_value())
            {
                return std::nullopt;
            }

            const std::time_t nowTime =
                std::chrono::system_clock::to_time_t(now);

            if (nowTime <= created.value())
            {
                return 1.0;
            }

            const double ageDays =
                static_cast<double>(
                    nowTime - created.value()) /
                (60.0 * 60.0 * 24.0);
            const double score =
                1.0 - ageDays / kFirstSeenWindowDays;

            return std::max(
                0.0,
                std::min(1.0, score));
        }
    } // namespace

    RankedJob ScoreJob(
        const JobSummary &job,
        const std::vector<JobRequirement> &requirements,
        const std::optional<UserProfile> &profile,
        const std::vector<UserSkill> &skills,
        const JobSearchFilter &filter,
        std::chrono::system_clock::time_point now)
    {
        std::unordered_map<
            std::string,
            std::int32_t> skillLevels;

        for (const UserSkill &skill : skills)
        {
            skillLevels.emplace(
                FoldAsciiCase(
                    Trim(skill.name)),
                skill.level);
        }

        RankedJob ranked;
        ranked.job = job;
        const std::optional<double> skillFit =
            SkillFit(
                requirements,
                skillLevels,
                &ranked.matchedSkills,
                &ranked.missingSkills);
        ranked.breakdown.skill =
            filter.skill.has_value()
                ? std::nullopt
                : skillFit;
        ranked.breakdown.title =
            TitleFit(profile, job.title);
        ranked.breakdown.salary =
            filter.salaryMin.has_value() ||
                    filter.salaryMax.has_value()
                ? std::nullopt
                : SalaryFit(profile, job);
        ranked.breakdown.location =
            filter.location.has_value()
                ? std::nullopt
                : LocationFit(profile, job);
        ranked.breakdown.firstSeen =
            FirstSeenFit(job, now);

        struct Component
        {
            std::optional<double> value;
            double weight = 0.0;
        };

        const Component components[] = {
            {ranked.breakdown.skill, kSkillWeight},
            {ranked.breakdown.title, kTitleWeight},
            {ranked.breakdown.salary, kSalaryWeight},
            {ranked.breakdown.location, kLocationWeight},
            {ranked.breakdown.firstSeen, kFirstSeenWeight}};

        double weightedScore = 0.0;
        double totalWeight = 0.0;

        for (const Component &component : components)
        {
            if (!component.value.has_value())
            {
                continue;
            }

            weightedScore +=
                component.value.value() *
                component.weight;
            totalWeight += component.weight;
        }

        ranked.score =
            totalWeight > 0.0
                ? 100.0 * weightedScore / totalWeight
                : 0.0;
        return ranked;
    }

} // namespace jobpilot::service
