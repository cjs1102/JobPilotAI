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
            // 缺失技能是"岗位要求"这一来源陈述的事实，不是对候选人的否定。
            content << "岗位要求待补充：" << Join(missing, "、")
                    << "（请确认后再写入简历）\n";

            for (const std::string &name : missing)
            {
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

        draft.content = content.str();
        return draft;
    }
} // namespace jobpilot::service
