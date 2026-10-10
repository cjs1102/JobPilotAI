// 0015 phase D step D2: tailor_rule_v1 is deterministic, provenance-tagged and
// never invents a fact.
//
// Pure unit tests: no database, no network, no clock, no AI key.

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "jobpilot/service/resume/tailor_rule.h"

namespace jobpilot::service
{
    namespace
    {
        UserProfile MakeProfile()
        {
            UserProfile profile;
            profile.userId = 7;
            profile.targetPosition = "C++后端开发";
            profile.experience = "3年";
            profile.location = "深圳";
            profile.salaryMin = 20;
            profile.salaryMax = 35;
            profile.summary = "熟悉 Linux 网络编程";
            profile.educationLevel = 3;
            profile.graduationYear = 2026;
            profile.campusStatus = 2;
            return profile;
        }

        std::vector<UserSkill> MakeSkills()
        {
            UserSkill first;
            first.id = 1;
            first.userId = 7;
            first.name = "C++";
            first.level = 3;

            UserSkill second;
            second.id = 2;
            second.userId = 7;
            second.name = "Redis";
            second.level = 1;

            return {first, second};
        }

        JobSummary MakeJob()
        {
            JobSummary job;
            job.id = 20002;
            job.company = "Beta";
            job.title = "C++ 后端";
            job.location = "深圳";
            job.deadline = "2028-01-01 00:00:00";
            return job;
        }

        std::vector<JobRequirement> MakeRequirements()
        {
            JobRequirement first;
            first.skill = "C++";
            first.level = 3;

            JobRequirement second;
            second.skill = "Kafka";
            second.level = 2;

            return {first, second};
        }

        bool IsAllowedSource(
            const std::string &source)
        {
            const std::vector<std::string> prefixes = {
                kTailorSourceProfile,
                kTailorSourceSkill,
                kTailorSourceRequirement,
                kTailorSourceJob};

            if (source == kTailorSourceResumeContent)
            {
                return true;
            }

            for (const std::string &prefix : prefixes)
            {
                if (source.rfind(prefix, 0) == 0 &&
                    source.size() > prefix.size())
                {
                    return true;
                }
            }

            return false;
        }
    } // namespace

    TEST(TailorRuleTest, ProducesADeterministicDraftWithNoAiInput)
    {
        const TailorDraft first =
            BuildTailorDraft(
                "张三",
                "zhang@example.invalid",
                MakeProfile(),
                MakeSkills(),
                "原始简历正文",
                MakeJob(),
                MakeRequirements());

        const TailorDraft second =
            BuildTailorDraft(
                "张三",
                "zhang@example.invalid",
                MakeProfile(),
                MakeSkills(),
                "原始简历正文",
                MakeJob(),
                MakeRequirements());

        // Same inputs -> byte-identical draft (determinism, no clock/random).
        EXPECT_EQ(first.content, second.content);
        EXPECT_EQ(first.facts.size(), second.facts.size());
        EXPECT_EQ(first.placeholders, second.placeholders);
        EXPECT_EQ(first.generator, "rule_v1");
        EXPECT_EQ(first.sourceVersion, "tailor_rule_v1");
        EXPECT_FALSE(first.content.empty());
    }

    TEST(TailorRuleTest, EveryFactCitesItsSourceAndAppearsInTheContent)
    {
        const TailorDraft draft =
            BuildTailorDraft(
                "张三",
                "zhang@example.invalid",
                MakeProfile(),
                MakeSkills(),
                "原始简历正文",
                MakeJob(),
                MakeRequirements());

        EXPECT_FALSE(draft.facts.empty());

        for (const TailorFact &fact : draft.facts)
        {
            EXPECT_TRUE(IsAllowedSource(fact.source))
                << fact.source;
            EXPECT_FALSE(fact.text.empty());
            EXPECT_NE(
                draft.content.find(fact.text),
                std::string::npos)
                << fact.text;
        }
    }

    TEST(TailorRuleTest, NothingDerivableBecomesAPlaceholderNotAClaim)
    {
        // Completely empty sources: no name, no profile, no skills, no resume
        // text, no job. The draft may only contain placeholder slots.
        const UserProfile emptyProfile;

        const TailorDraft draft =
            BuildTailorDraft(
                "",
                "",
                emptyProfile,
                {},
                "",
                JobSummary{},
                {});

        EXPECT_FALSE(draft.placeholders.empty());
        EXPECT_NE(
            draft.content.find("【请填写：姓名】"),
            std::string::npos);
        EXPECT_NE(
            draft.content.find("【请填写：最高学历】"),
            std::string::npos);
        EXPECT_NE(
            draft.content.find("【请填写：简历正文】"),
            std::string::npos);

        // No fact may be asserted when there is no source for it.
        EXPECT_TRUE(draft.facts.empty());

        // No invented value: nothing that looks like a fabricated phone number,
        // email or year appears.
        EXPECT_EQ(draft.content.find('@'), std::string::npos);

        for (char digit = '0'; digit <= '9'; ++digit)
        {
            EXPECT_EQ(
                draft.content.find(digit),
                std::string::npos)
                << digit;
        }
    }

    TEST(TailorRuleTest, RequirementsBecomeMatchedAndMissingStatements)
    {
        const TailorDraft draft =
            BuildTailorDraft(
                "",
                "",
                MakeProfile(),
                MakeSkills(),
                "",
                MakeJob(),
                MakeRequirements());

        // C++ is owned (skill record) -> matched; Kafka is only required.
        EXPECT_NE(
            draft.content.find("岗位要求已具备：C++"),
            std::string::npos);
        EXPECT_NE(
            draft.content.find("岗位要求待补充：Kafka"),
            std::string::npos);

        bool krakenFact = false;

        for (const TailorFact &fact : draft.facts)
        {
            if (fact.source == "requirement:Kafka")
            {
                krakenFact = true;
            }
        }

        EXPECT_TRUE(krakenFact);
    }

    TEST(TailorRuleTest, RendersTheAtsPlainTextContract)
    {
        const TailorDraft draft =
            BuildTailorDraft(
                "张三",
                "zhang@example.invalid",
                MakeProfile(),
                MakeSkills(),
                "原始简历正文",
                MakeJob(),
                MakeRequirements());

        // Stable headings in a fixed order (ATS can key on them).
        const std::vector<std::string> headings = {
            "【个人信息】",
            "【求职意向】",
            "【技能】",
            "【教育背景】",
            "【自我评价】",
            "【职位匹配说明】",
            "【原始简历文本】"};

        std::size_t previous = 0;
        bool firstHeading = true;

        for (const std::string &heading : headings)
        {
            const std::size_t position =
                draft.content.find(heading);

            ASSERT_NE(position, std::string::npos) << heading;

            if (!firstHeading)
            {
                EXPECT_GT(position, previous) << heading;
            }

            firstHeading = false;
            previous = position;
        }

        // Plain text: no tabs, no table separators, no images-only content.
        EXPECT_EQ(draft.content.find('\t'), std::string::npos);
        EXPECT_EQ(draft.content.find('|'), std::string::npos);
        EXPECT_EQ(draft.content.find("<table"), std::string::npos);

        // The chosen resume version's text is quoted verbatim.
        EXPECT_NE(
            draft.content.find("原始简历正文"),
            std::string::npos);

        // Profile facts are rendered from the profile, not re-derived.
        EXPECT_NE(
            draft.content.find("期望薪资：20-35K"),
            std::string::npos);
        EXPECT_NE(
            draft.content.find("学历：本科"),
            std::string::npos);
        EXPECT_NE(
            draft.content.find("毕业年份：2026"),
            std::string::npos);
        EXPECT_NE(
            draft.content.find("应届状态：应届"),
            std::string::npos);
    }

} // namespace jobpilot::service
