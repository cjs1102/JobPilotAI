#include "jobpilot/service/ai/match_rule.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "jobpilot/service/job/job_ranking.h"

namespace jobpilot::service
{
    namespace
    {
        // A component at or above this value is reported as a strength.
        constexpr double kEvidenceThreshold = 0.5;

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

            return value.substr(begin, end - begin);
        }

        bool IsWordCharacter(char character)
        {
            return std::isalnum(
                       static_cast<unsigned char>(character)) != 0;
        }

        void AddEvidence(
            std::vector<MatchEvidenceItem> *items,
            const std::string &text,
            const char *evidence)
        {
            items->push_back(
                MatchEvidenceItem{text, evidence});
        }

        void AddComponentEvidence(
            MatchResult *result,
            const std::optional<double> &component,
            const std::string &strength,
            const std::string &weakness,
            const std::string &missing)
        {
            if (!component.has_value())
            {
                AddEvidence(
                    &result->gaps,
                    missing,
                    kEvidenceProfile);
                return;
            }

            if (component.value() >= kEvidenceThreshold)
            {
                AddEvidence(
                    &result->strengths,
                    strength,
                    kEvidenceProfile);
                return;
            }

            AddEvidence(
                &result->gaps,
                weakness,
                kEvidenceProfile);
        }
    } // namespace

    bool ResumeMentionsSkill(
        const std::string &resumeContent,
        const std::string &skill)
    {
        const std::string needle =
            FoldAsciiCase(Trim(skill));

        if (needle.empty())
        {
            return false;
        }

        const std::string haystack =
            FoldAsciiCase(resumeContent);

        if (haystack.empty())
        {
            return false;
        }

        std::size_t position = haystack.find(needle);

        while (position != std::string::npos)
        {
            const bool startsAtBoundary =
                !IsWordCharacter(needle.front()) ||
                position == 0 ||
                !IsWordCharacter(haystack[position - 1]);

            const std::size_t end = position + needle.size();

            const bool endsAtBoundary =
                !IsWordCharacter(needle.back()) ||
                end >= haystack.size() ||
                !IsWordCharacter(haystack[end]);

            if (startsAtBoundary && endsAtBoundary)
            {
                return true;
            }

            position = haystack.find(needle, position + 1);
        }

        return false;
    }

    MatchResult ScoreMatch(
        const JobSummary &job,
        const std::vector<JobRequirement> &requirements,
        const std::optional<UserProfile> &profile,
        const std::vector<UserSkill> &skills,
        const std::string &resumeContent,
        std::chrono::system_clock::time_point now)
    {
        const RankedJob ranked = ScoreJob(
            job,
            requirements,
            profile,
            skills,
            JobSearchFilter{},
            now);

        MatchResult result;
        result.matchedSkills = ranked.matchedSkills;
        result.missingSkills = ranked.missingSkills;
        result.breakdown.skill = ranked.breakdown.skill;
        result.breakdown.title = ranked.breakdown.title;
        result.breakdown.salary = ranked.breakdown.salary;
        result.breakdown.location = ranked.breakdown.location;

        struct Component
        {
            const std::optional<double> &value;
            double weight = 0.0;
        };

        const Component components[] = {
            {result.breakdown.skill, kRuleV1SkillWeight},
            {result.breakdown.title, kRuleV1TitleWeight},
            {result.breakdown.salary, kRuleV1SalaryWeight},
            {result.breakdown.location, kRuleV1LocationWeight}};

        double weightedScore = 0.0;
        double totalWeight = 0.0;

        for (const Component &component : components)
        {
            if (!component.value.has_value())
            {
                continue;
            }

            // Defensive: component scores are documented 0..1.
            const double value = std::min(
                1.0,
                std::max(0.0, component.value.value()));

            weightedScore += value * component.weight;
            totalWeight += component.weight;
        }

        result.score =
            totalWeight > 0.0
                ? std::min(
                      100.0,
                      std::round(
                          100.0 * weightedScore / totalWeight))
                : 0.0;

        if (requirements.empty())
        {
            AddEvidence(
                &result.gaps,
                "Job has no structured skill requirements; "
                "skill fit cannot be evaluated",
                kEvidenceJobRequirements);
        }
        else if (ranked.matchedSkills.empty())
        {
            AddEvidence(
                &result.gaps,
                "No required skill is covered by the profile",
                kEvidenceJobRequirements);
        }
        else
        {
            AddEvidence(
                &result.strengths,
                "Matched " +
                    std::to_string(ranked.matchedSkills.size()) +
                    "/" +
                    std::to_string(requirements.size()) +
                    " required skills",
                kEvidenceSkills);
        }

        for (const std::string &skill : ranked.missingSkills)
        {
            AddEvidence(
                &result.gaps,
                "Missing required skill: " + skill,
                kEvidenceJobRequirements);
        }

        if (!profile.has_value())
        {
            AddEvidence(
                &result.gaps,
                "Profile is empty; position, location and salary fit "
                "cannot be evaluated",
                kEvidenceProfile);
        }
        else
        {
            AddComponentEvidence(
                &result,
                result.breakdown.title,
                "Title relevance to the target position is high",
                "Title is only loosely related to the target position",
                "Profile has no target position; title fit cannot be "
                "evaluated");
            AddComponentEvidence(
                &result,
                result.breakdown.salary,
                "Salary expectation overlaps the posting range",
                "Salary expectation does not overlap the posting range",
                "Profile has no salary expectation; salary fit cannot be "
                "evaluated");
            AddComponentEvidence(
                &result,
                result.breakdown.location,
                "Location matches the profile",
                "Location does not match the profile",
                "Profile has no location; location fit cannot be evaluated");
        }

        result.resumeEvidence.reserve(requirements.size());

        for (const JobRequirement &requirement : requirements)
        {
            result.resumeEvidence.push_back(
                MatchResumeEvidence{
                    requirement.skill,
                    ResumeMentionsSkill(
                        resumeContent,
                        requirement.skill)});
        }

        return result;
    }

} // namespace jobpilot::service
