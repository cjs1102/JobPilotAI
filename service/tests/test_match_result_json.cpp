#include "jobpilot/service/ai/match_result_json.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace jobpilot::service
{
    namespace
    {
        using Json = nlohmann::json;

        Json Parse(
            const std::string &value)
        {
            return Json::parse(value);
        }

        MatchResult MakeFullResult()
        {
            MatchResult result;
            result.score = 82.5;
            result.breakdown.skill = 0.91;
            result.breakdown.title = 0.75;
            result.breakdown.salary = 1.0;
            result.breakdown.location = 1.0;
            result.matchedSkills = {"C++", "Redis"};
            result.missingSkills = {"Kubernetes"};
            result.strengths = {
                {"Matched 2/3 required skills",
                 "skills"}};
            result.gaps = {
                {"Missing required skill: Kubernetes",
                 "job_requirements"}};
            result.resumeEvidence = {
                {"Redis", true},
                {"Kubernetes", false}};
            return result;
        }
    } // namespace

    TEST(MatchResultJsonTest, EmitsTheDocumentedKeyOrder)
    {
        const std::string serialized =
            SerializeMatchResult(MakeFullResult());

        EXPECT_EQ(
            serialized,
            R"({"score_version":"match_rule_v1",)"
            R"("score":82.5,)"
            R"("source":"rule",)"
            R"("breakdown":{"skill":0.91,"title":0.75,)"
            R"("salary":1.0,"location":1.0},)"
            R"("matched_skills":["C++","Redis"],)"
            R"("missing_skills":["Kubernetes"],)"
            R"("strengths":[{"text":"Matched 2/3 required skills",)"
            R"("evidence":"skills"}],)"
            R"("gaps":[{"text":"Missing required skill: Kubernetes",)"
            R"("evidence":"job_requirements"}],)"
            R"("resume_evidence":[{"skill":"Redis","mentioned":true},)"
            R"({"skill":"Kubernetes","mentioned":false}]})");
    }

    TEST(MatchResultJsonTest, IsDeterministicForTheSameInput)
    {
        const MatchResult result = MakeFullResult();

        EXPECT_EQ(
            SerializeMatchResult(result),
            SerializeMatchResult(result));
    }

    TEST(MatchResultJsonTest, OmitsAbsentBreakdownComponents)
    {
        MatchResult result;
        result.score = 61.0;
        result.breakdown.title = 1.0;
        result.matchedSkills = {"C++"};

        const Json parsed =
            Parse(SerializeMatchResult(result));

        ASSERT_TRUE(parsed.contains("breakdown"));
        ASSERT_TRUE(parsed["breakdown"].is_object());
        EXPECT_EQ(parsed["breakdown"].size(), 1U);
        EXPECT_TRUE(parsed["breakdown"].contains("title"));
        EXPECT_FALSE(parsed["breakdown"].contains("skill"));
        EXPECT_FALSE(parsed["breakdown"].contains("salary"));
        EXPECT_FALSE(parsed["breakdown"].contains("location"));
    }

    TEST(MatchResultJsonTest, KeepsEmptyCollectionsAndBreakdown)
    {
        const MatchResult result;

        const std::string serialized =
            SerializeMatchResult(result);

        EXPECT_EQ(
            serialized,
            R"({"score_version":"match_rule_v1",)"
            R"("score":0.0,)"
            R"("source":"rule",)"
            R"("breakdown":{},)"
            R"("matched_skills":[],)"
            R"("missing_skills":[],)"
            R"("strengths":[],)"
            R"("gaps":[],)"
            R"("resume_evidence":[]})");

        const Json parsed = Parse(serialized);

        EXPECT_TRUE(parsed["breakdown"].is_object());
        EXPECT_TRUE(parsed["breakdown"].empty());
        EXPECT_TRUE(parsed["strengths"].is_array());
        EXPECT_TRUE(parsed["gaps"].is_array());
        EXPECT_TRUE(parsed["resume_evidence"].is_array());
    }

    TEST(MatchResultJsonTest, RoundTripsEveryField)
    {
        const MatchResult result = MakeFullResult();

        const Json parsed =
            Parse(SerializeMatchResult(result));

        EXPECT_EQ(
            parsed["score_version"].get<std::string>(),
            "match_rule_v1");
        EXPECT_DOUBLE_EQ(
            parsed["score"].get<double>(),
            82.5);
        EXPECT_EQ(
            parsed["source"].get<std::string>(),
            "rule");
        EXPECT_DOUBLE_EQ(
            parsed["breakdown"]["skill"].get<double>(),
            0.91);
        EXPECT_DOUBLE_EQ(
            parsed["breakdown"]["title"].get<double>(),
            0.75);
        EXPECT_DOUBLE_EQ(
            parsed["breakdown"]["salary"].get<double>(),
            1.0);
        EXPECT_DOUBLE_EQ(
            parsed["breakdown"]["location"].get<double>(),
            1.0);
        EXPECT_EQ(
            parsed["matched_skills"],
            Json({"C++", "Redis"}));
        EXPECT_EQ(
            parsed["missing_skills"],
            Json({"Kubernetes"}));

        ASSERT_EQ(parsed["strengths"].size(), 1U);
        EXPECT_EQ(
            parsed["strengths"][0]["text"].get<std::string>(),
            "Matched 2/3 required skills");
        EXPECT_EQ(
            parsed["strengths"][0]["evidence"].get<std::string>(),
            "skills");

        ASSERT_EQ(parsed["gaps"].size(), 1U);
        EXPECT_EQ(
            parsed["gaps"][0]["evidence"].get<std::string>(),
            "job_requirements");

        ASSERT_EQ(parsed["resume_evidence"].size(), 2U);
        EXPECT_EQ(
            parsed["resume_evidence"][0]["skill"].get<std::string>(),
            "Redis");
        EXPECT_TRUE(
            parsed["resume_evidence"][0]["mentioned"].get<bool>());
        EXPECT_EQ(
            parsed["resume_evidence"][1]["skill"].get<std::string>(),
            "Kubernetes");
        EXPECT_FALSE(
            parsed["resume_evidence"][1]["mentioned"].get<bool>());
    }

    TEST(MatchResultJsonTest, KeepsNumberPrecision)
    {
        MatchResult result;
        result.score = 100.0 / 3.0;
        result.breakdown.skill = 0.1 + 0.2;
        result.breakdown.salary = 1.0e17 + 1.0;

        const Json parsed =
            Parse(SerializeMatchResult(result));

        EXPECT_DOUBLE_EQ(
            parsed["score"].get<double>(),
            100.0 / 3.0);
        EXPECT_DOUBLE_EQ(
            parsed["breakdown"]["skill"].get<double>(),
            0.1 + 0.2);
        EXPECT_DOUBLE_EQ(
            parsed["breakdown"]["salary"].get<double>(),
            1.0e17 + 1.0);

        EXPECT_NE(
            SerializeMatchResult(result).find("0.30000000000000004"),
            std::string::npos);
    }

    TEST(MatchResultJsonTest, EscapesAndPreservesUtf8)
    {
        const std::string quoted =
            "He said \"C++\" \\ and\nline\ttab";
        const std::string utf8 = "caf\xc3\xa9 \xe4\xb8\xad\xe6\x96\x87";

        MatchResult result;
        result.strengths = {
            {quoted, "skills"},
            {utf8, "resume_text"}};

        const std::string serialized =
            SerializeMatchResult(result);

        const Json parsed = Parse(serialized);

        ASSERT_EQ(parsed["strengths"].size(), 2U);
        EXPECT_EQ(
            parsed["strengths"][0]["text"].get<std::string>(),
            quoted);
        EXPECT_EQ(
            parsed["strengths"][1]["text"].get<std::string>(),
            utf8);
        EXPECT_EQ(
            parsed["strengths"][0]["evidence"].get<std::string>(),
            "skills");

        EXPECT_NE(
            serialized.find("\\\""),
            std::string::npos);
        EXPECT_NE(
            serialized.find("\\n"),
            std::string::npos);
        EXPECT_NE(
            serialized.find("\\t"),
            std::string::npos);
    }

} // namespace jobpilot::service
