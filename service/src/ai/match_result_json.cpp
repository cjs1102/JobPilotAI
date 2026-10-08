#include "jobpilot/service/ai/match_result_json.h"

#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace jobpilot::service
{
    namespace
    {
        // ordered_json keeps the documented key order deterministic instead of
        // sorting keys alphabetically (ExecPlan 0013 section 5.5).
        using Json = nlohmann::ordered_json;

        Json EvidenceToJson(
            const std::vector<MatchEvidenceItem> &items)
        {
            Json array = Json::array();

            for (const MatchEvidenceItem &item : items)
            {
                array.push_back(
                    Json{
                        {"text", item.text},
                        {"evidence", item.evidence}});
            }

            return array;
        }

        Json ResumeEvidenceToJson(
            const std::vector<MatchResumeEvidence> &items)
        {
            Json array = Json::array();

            for (const MatchResumeEvidence &item : items)
            {
                array.push_back(
                    Json{
                        {"skill", item.skill},
                        {"mentioned", item.mentioned}});
            }

            return array;
        }

        Json BreakdownToJson(
            const MatchBreakdown &breakdown)
        {
            Json object = Json::object();

            if (breakdown.skill.has_value())
            {
                object["skill"] = breakdown.skill.value();
            }

            if (breakdown.title.has_value())
            {
                object["title"] = breakdown.title.value();
            }

            if (breakdown.salary.has_value())
            {
                object["salary"] = breakdown.salary.value();
            }

            if (breakdown.location.has_value())
            {
                object["location"] = breakdown.location.value();
            }

            return object;
        }
    } // namespace

    std::string SerializeMatchResult(
        const MatchResult &result)
    {
        const Json document{
            {"score_version", result.scoreVersion},
            {"score", result.score},
            {"source", result.source},
            {"breakdown", BreakdownToJson(result.breakdown)},
            {"matched_skills", result.matchedSkills},
            {"missing_skills", result.missingSkills},
            {"strengths", EvidenceToJson(result.strengths)},
            {"gaps", EvidenceToJson(result.gaps)},
            {"resume_evidence", ResumeEvidenceToJson(result.resumeEvidence)}};

        return document.dump();
    }

} // namespace jobpilot::service
