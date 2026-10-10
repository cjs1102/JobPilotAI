#include "jobpilot/service/resume/tailor_rule.h"

#include <algorithm>
#include <cstdint>
#include <sstream>

namespace jobpilot::service
{
    namespace
    {
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
                return "";
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
                return "";
            }
        }

        std::string Join(
            const std::vector<std::string> &values,
            const char *separator)
        {
            std::string joined;

            for (const std::string &value : values)
            {
                if (value.empty())
                {
                    continue;
                }

                if (!joined.empty())
                {
                    joined += separator;
                }

                joined += value;
            }

            return joined;
        }

        // Folded comparison for skill matching (case-insensitive, trimmed).
        std::string Fold(
            const std::string &value)
        {
            std::string folded;

            for (const char character : value)
            {
                if (character == ' ' || character == '\t')
                {
                    continue;
                }

                folded.push_back(
                    static_cast<char>(
                        std::tolower(
                            static_cast<unsigned char>(character))));
            }

            return folded;
        }

        void AddPlaceholder(
            TailorDraft *draft,
            const std::string &label,
            std::string *target)
        {
            draft->placeholders.push_back(label);
            *target = "【请填写：" + label + "】";
        }

        void AddFact(
            TailorDraft *draft,
            const std::string &source,
            const std::string &text)
        {
            draft->facts.push_back(
                TailorFact{text, source});
        }
    } // namespace

    TailorDraft BuildTailorDraft(
        const std::string &candidateName,
        const std::string &candidateEmail,
        const UserProfile &profile,
        const std::vector<UserSkill> &skills,
        const std::string &resumeContent,
        const JobSummary &job,
        const std::vector<JobRequirement> &requirements)
    {
        TailorDraft draft;

        std::ostringstream content;

        // ---- 头部：只写来源里真实存在的姓名/邮箱，否则占位 ----
        std::string name = candidateName;
        std::string email = candidateEmail;

        if (name.empty())
        {
            AddPlaceholder(&draft, "姓名", &name);
        }
        else
        {
            AddFact(&draft, "profile:name", name);
        }

        if (email.empty())
        {
            AddPlaceholder(&draft, "联系方式", &email);
        }
        else
        {
            AddFact(&draft, "profile:email", email);
        }

        // ---- 稳定的小节顺序（ATS 可 key 的固定标题）----
        content << "【个人信息】\n"
                << "姓名：" << name << "\n"
                << "联系方式：" << email << "\n\n";

        content << "【求职意向】\n";

        if (profile.targetPosition.has_value() &&
            !profile.targetPosition.value().empty())
        {
            content << "目标岗位："
                    << profile.targetPosition.value() << "\n";
            AddFact(
                &draft,
                "profile:target_position",
                profile.targetPosition.value());
        }
        else
        {
            std::string target;
            AddPlaceholder(&draft, "目标岗位", &target);
            content << "目标岗位：" << target << "\n";
        }

        if (profile.location.has_value() &&
            !profile.location.value().empty())
        {
            content << "期望城市："
                    << profile.location.value() << "\n";
            AddFact(
                &draft,
                "profile:location",
                profile.location.value());
        }
        else
        {
            std::string location;
            AddPlaceholder(&draft, "期望城市", &location);
            content << "期望城市：" << location << "\n";
        }

        if (profile.salaryMin.has_value() &&
            profile.salaryMax.has_value())
        {
            const std::string range =
                std::to_string(profile.salaryMin.value()) +
                "-" +
                std::to_string(profile.salaryMax.value()) +
                "K";
            content << "期望薪资：" << range << "\n";
            AddFact(&draft, "profile:salary", range);
        }
        else
        {
            std::string salary;
            AddPlaceholder(&draft, "期望薪资", &salary);
            content << "期望薪资：" << salary << "\n";
        }

        content << "\n【技能】\n";

        std::vector<std::string> skillNames;
        skillNames.reserve(skills.size());

        for (const UserSkill &skill : skills)
        {
            if (skill.name.empty())
            {
                continue;
            }

            skillNames.push_back(skill.name);
            AddFact(&draft, std::string(kTailorSourceSkill) + skill.name,
                    skill.name);
        }

        if (skillNames.empty())
        {
            std::string skillLine;
            AddPlaceholder(&draft, "技能", &skillLine);
            content << skillLine << "\n";
        }
        else
        {
            content << "掌握技能：" << Join(skillNames, "、") << "\n";
        }

        // 岗位要求与已有技能的确定性交集：只陈述来源，不推断水平。
        std::vector<std::string> matched;
        std::vector<std::string> missing;

        for (const JobRequirement &requirement : requirements)
        {
            if (requirement.skill.empty())
            {
                continue;
            }

            const std::string folded = Fold(requirement.skill);
            const bool hasSkill = std::any_of(
                skillNames.begin(),
                skillNames.end(),
                [&folded](const std::string &name)
                {
                    return Fold(name) == folded;
                });

            if (hasSkill)
            {
                matched.push_back(requirement.skill);
                AddFact(
                    &draft,
                    std::string(kTailorSourceRequirement) +
                        requirement.skill,
                    requirement.skill);
            }
            else
            {
                missing.push_back(requirement.skill);
            }
        }

        if (!matched.empty())
        {
            content << "岗位要求已具备：" << Join(matched, "、") << "\n";
        }

        if (!missing.empty())
        {
            // 岗位要求待补充：这是"岗位要求"来源的事实，不是对候选人的断言；
            // 括号说明留在标签里，缺失技能各自成行（事实门按行校验）。
            content << "岗位要求待补充（以下为岗位要求，请在简历中确认后再体现）：\n";

            for (const std::string &name : missing)
            {
                content << name << "\n";
                AddFact(
                    &draft,
                    std::string(kTailorSourceRequirement) + name,
                    name);
            }
        }

        content << "\n【教育背景】\n";

        if (profile.educationLevel.has_value() &&
            EducationName(profile.educationLevel.value())[0] != '\0')
        {
            const std::string level =
                EducationName(profile.educationLevel.value());
            content << "学历：" << level << "\n";
            AddFact(&draft, "profile:education_level", level);
        }
        else
        {
            std::string level;
            AddPlaceholder(&draft, "最高学历", &level);
            content << "学历：" << level << "\n";
        }

        if (profile.graduationYear.has_value())
        {
            const std::string year =
                std::to_string(profile.graduationYear.value());
            content << "毕业年份：" << year << "\n";
            AddFact(&draft, "profile:graduation_year", year);
        }
        else
        {
            std::string year;
            AddPlaceholder(&draft, "毕业年份", &year);
            content << "毕业年份：" << year << "\n";
        }

        if (profile.campusStatus.has_value() &&
            CampusName(profile.campusStatus.value())[0] != '\0')
        {
            const std::string campus =
                CampusName(profile.campusStatus.value());
            content << "应届状态：" << campus << "\n";
            AddFact(&draft, "profile:campus_status", campus);
        }
        else
        {
            std::string campus;
            AddPlaceholder(&draft, "应届状态", &campus);
            content << "应届状态：" << campus << "\n";
        }

        content << "\n【自我评价】\n";

        if (profile.summary.has_value() &&
            !profile.summary.value().empty())
        {
            content << profile.summary.value() << "\n";
            AddFact(&draft, "profile:summary", profile.summary.value());
        }
        else
        {
            std::string summary;
            AddPlaceholder(&draft, "自我评价", &summary);
            content << summary << "\n";
        }

        content << "\n【职位匹配说明】\n";

        if (!job.title.empty())
        {
            content << "目标职位：" << job.title << "\n";
            AddFact(&draft, "job:title", job.title);
        }

        if (!job.company.empty())
        {
            content << "招聘方：" << job.company << "\n";
            AddFact(&draft, "job:company", job.company);
        }

        if (job.title.empty() && job.company.empty())
        {
            std::string target;
            AddPlaceholder(&draft, "目标职位", &target);
            content << "目标职位：" << target << "\n";
        }

        content << "\n【原始简历文本】\n";

        if (resumeContent.empty())
        {
            std::string base;
            AddPlaceholder(&draft, "简历正文", &base);
            content << base << "\n";
        }
        else
        {
            content << "原始简历文本（逐字引用）：\n"
                    << resumeContent << "\n";
            AddFact(&draft, kTailorSourceResumeContent,
                    "原始简历文本（逐字引用）");
        }

        // Explicit terminator: everything between the marker and this line is
        // the verbatim quote; anything AFTER it must be sourced again (so a
        // wording pass cannot append unchecked claims behind the quote).
        content << "【原始简历文本结束】\n";

        draft.content = content.str();
        return draft;
    }
} // namespace jobpilot::service

namespace jobpilot::service
{
    namespace
    {
        constexpr const char *kQuotedBlockMarker =
            "原始简历文本（逐字引用）：";
        constexpr const char *kQuotedBlockEnd =
            "【原始简历文本结束】";
        constexpr const char *kPlaceholderPrefix = "【请填写：";
        constexpr const char *kPlaceholderSuffix = "】";

        bool StartsWith(
            const std::string &value,
            const char *prefix)
        {
            const std::string head(prefix);
            return value.size() >= head.size() &&
                   value.compare(0, head.size(), head) == 0;
        }

        bool EndsWith(
            const std::string &value,
            const char *suffix)
        {
            const std::string tail(suffix);
            return value.size() >= tail.size() &&
                   value.compare(
                       value.size() - tail.size(),
                       tail.size(),
                       tail) == 0;
        }

        bool IsHeading(
            const std::string &line)
        {
            return EndsWith(line, "】") &&
                   line.find("【请填写：") == std::string::npos;
        }

        std::string TrimCopy(
            const std::string &value)
        {
            std::size_t begin = 0;
            std::size_t end = value.size();

            while (begin < end &&
                   (value[begin] == ' ' || value[begin] == '\r' ||
                    value[begin] == '\t'))
            {
                ++begin;
            }

            while (end > begin &&
                   (value[end - 1] == ' ' || value[end - 1] == '\r' ||
                    value[end - 1] == '\t'))
            {
                --end;
            }

            return value.substr(begin, end - begin);
        }

        // The claim part of a line: everything after the first full-width colon.
        std::string ClaimPart(
            const std::string &line)
        {
            const std::size_t colon = line.find("：");

            if (colon == std::string::npos)
            {
                return line;
            }

            return line.substr(colon + 3);
        }

        void EraseAll(
            std::string *text,
            const std::string &needle)
        {
            if (needle.empty())
            {
                return;
            }

            std::size_t position = text->find(needle);

            while (position != std::string::npos)
            {
                text->erase(position, needle.size());
                position = text->find(needle);
            }
        }

        // Separators and punctuation that may remain once the sourced facts are
        // removed from a line (labels live before the colon and are exempt).
        bool IsFiller(
            const std::string &value)
        {
            static const std::string fillers =
                "、，,;；:：()（）/| ";

            for (const char character : value)
            {
                if (fillers.find(character) == std::string::npos)
                {
                    return false;
                }
            }

            return true;
        }

        bool IsAllowedSource(
            const std::string &source)
        {
            if (source == kTailorSourceResumeContent)
            {
                return true;
            }

            const char *prefixes[] = {
                kTailorSourceProfile,
                kTailorSourceSkill,
                kTailorSourceRequirement,
                kTailorSourceJob};

            for (const char *prefix : prefixes)
            {
                if (StartsWith(source, prefix) &&
                    source.size() > std::string(prefix).size())
                {
                    return true;
                }
            }

            return false;
        }

        std::string JsonStringArray(
            const std::vector<std::string> &values)
        {
            std::string json = "[";

            for (std::size_t index = 0;
                 index < values.size();
                 ++index)
            {
                if (index > 0)
                {
                    json += ", ";
                }

                json += "\"";

                for (const char character : values[index])
                {
                    if (character == '"' || character == '\\')
                    {
                        json += '\\';
                    }

                    if (character == '\n')
                    {
                        json += "\\n";
                        continue;
                    }

                    json += character;
                }

                json += "\"";
            }

            json += "]";
            return json;
        }
    } // namespace

    FactCheckResult FactCheck(
        const TailorDraft &draft)
    {
        FactCheckResult result;

        // 1) Every fact must cite an allowed source and appear in the content.
        for (const TailorFact &fact : draft.facts)
        {
            if (!IsAllowedSource(fact.source))
            {
                result.unknownSources.push_back(fact.source);
            }

            if (fact.text.empty() ||
                draft.content.find(fact.text) == std::string::npos)
            {
                result.missingFacts.push_back(fact.text);
            }
        }

        // 2) Every recorded placeholder must actually appear as a slot.
        for (const std::string &label : draft.placeholders)
        {
            const std::string marker =
                std::string(kPlaceholderPrefix) + label +
                kPlaceholderSuffix;

            if (draft.content.find(marker) == std::string::npos)
            {
                result.missingPlaceholders.push_back(label);
            }
        }

        // 3) Every content line must be a heading, an explicit placeholder, a
        //    quoted resume block, or covered by source facts.
        std::size_t position = 0;
        bool inQuotedBlock = false;

        while (position <= draft.content.size())
        {
            const std::size_t newline =
                draft.content.find('\n', position);
            const std::size_t end =
                newline == std::string::npos
                    ? draft.content.size()
                    : newline;
            const std::string line = TrimCopy(
                draft.content.substr(position, end - position));

            position = end + 1;

            if (inQuotedBlock)
            {
                if (line == kQuotedBlockEnd)
                {
                    inQuotedBlock = false;
                }

                continue;
            }

            if (line.empty() || IsHeading(line))
            {
                continue;
            }

            if (line.find(kPlaceholderPrefix) != std::string::npos)
            {
                continue;
            }

            if (line == kQuotedBlockMarker)
            {
                inQuotedBlock = true;
                continue;
            }

            std::string remainder = ClaimPart(line);

            // Longest-first: a short fact text ("C++") must not erase a part
            // of a longer sourced claim ("C++后端开发") before it is matched.
            std::vector<std::string> needles;
            needles.reserve(draft.facts.size());

            for (const TailorFact &fact : draft.facts)
            {
                needles.push_back(fact.text);
            }

            std::sort(
                needles.begin(),
                needles.end(),
                [](const std::string &left, const std::string &right)
                {
                    return left.size() > right.size();
                });

            for (const std::string &needle : needles)
            {
                EraseAll(&remainder, needle);
            }

            remainder = TrimCopy(remainder);

            if (!remainder.empty() && !IsFiller(remainder))
            {
                result.unsourcedClaims.push_back(line);
            }
        }

        result.passed =
            result.unsourcedClaims.empty() &&
            result.unknownSources.empty() &&
            result.missingFacts.empty() &&
            result.missingPlaceholders.empty();
        result.status = result.passed ? 1 : 2;

        result.reportJson =
            std::string("{\"status\": \"") +
            (result.passed ? "pass" : "fail") +
            "\", \"facts\": " + std::to_string(draft.facts.size()) +
            ", \"placeholders\": " +
            std::to_string(draft.placeholders.size()) +
            ", \"unsourced_claims\": " +
            JsonStringArray(result.unsourcedClaims) +
            ", \"unknown_sources\": " +
            JsonStringArray(result.unknownSources) +
            ", \"missing_facts\": " +
            JsonStringArray(result.missingFacts) +
            ", \"missing_placeholders\": " +
            JsonStringArray(result.missingPlaceholders) +
            "}";

        return result;
    }
} // namespace jobpilot::service
