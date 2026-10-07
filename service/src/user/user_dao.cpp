#include "jobpilot/service/user/user_dao.h"

#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "jobpilot/database/mysql_pool.h"

namespace jobpilot::service
{
    namespace
    {
        std::string RequireValue(
            const jobpilot::core::Result::Value &value,
            const std::string &field)
        {
            if (!value.has_value())
            {
                throw std::runtime_error(
                    field + " must not be NULL");
            }

            return value.value();
        }

        std::uint64_t ParseUint64(
            const jobpilot::core::Result::Value &value,
            const std::string &field)
        {
            const std::string text =
                RequireValue(value, field);

            std::size_t parsed = 0;

            const unsigned long long userId =
                std::stoull(text, &parsed);

            if (parsed != text.size())
            {
                throw std::runtime_error(
                    field + " contains invalid characters");
            }

            return static_cast<std::uint64_t>(
                userId);
        }

        std::int32_t ParseStatus(
            const jobpilot::core::Result::Value &value)
        {
            const std::string text =
                RequireValue(value, "users.status");

            std::size_t parsed = 0;

            const long status =
                std::stol(text, &parsed);

            if (parsed != text.size())
            {
                throw std::runtime_error(
                    "users.status contains invalid characters");
            }

            return static_cast<std::int32_t>(
                status);
        }

        std::int32_t ParseInt32(
            const jobpilot::core::Result::Value &value,
            const std::string &field)
        {
            const std::string text =
                RequireValue(value, field);

            std::size_t parsed = 0;

            const long result =
                std::stol(text, &parsed);

            if (parsed != text.size() ||
                result <
                    std::numeric_limits<
                        std::int32_t>::min() ||
                result >
                    std::numeric_limits<
                        std::int32_t>::max())
            {
                throw std::runtime_error(
                    field + " contains an invalid value");
            }

            return static_cast<std::int32_t>(
                result);
        }

        std::uint32_t ParseUnsigned32(
            const jobpilot::core::Result::Value &value,
            const std::string &field)
        {
            const std::string text =
                RequireValue(value, field);

            std::size_t parsed = 0;

            const unsigned long long result =
                std::stoull(text, &parsed);

            if (parsed != text.size() ||
                result >
                    std::numeric_limits<
                        std::uint32_t>::max())
            {
                throw std::runtime_error(
                    field + " contains an invalid value");
            }

            return static_cast<std::uint32_t>(
                result);
        }

        jobpilot::core::SqlParam OptionalStringParam(
            const std::optional<std::string> &value)
        {
            if (value.has_value())
            {
                return value.value();
            }

            return nullptr;
        }

        jobpilot::core::SqlParam OptionalUnsignedParam(
            const std::optional<std::uint32_t> &value)
        {
            if (value.has_value())
            {
                return static_cast<std::uint64_t>(
                    value.value());
            }

            return nullptr;
        }

        UserRecord MakeUserRecord(
            const jobpilot::core::Result &result)
        {
            if (result.rowCount() != 1)
            {
                throw std::runtime_error(
                    "user query returned an unexpected row count");
            }

            const auto &row = result.row(0);

            if (row.size() != 8)
            {
                throw std::runtime_error(
                    "user query returned an unexpected column count");
            }

            UserRecord user;
            user.id = ParseUint64(row[0], "users.id");
            user.username =
                RequireValue(row[1], "users.username");
            user.email =
                RequireValue(row[2], "users.email");
            user.passwordHash =
                RequireValue(row[3], "users.password_hash");

            if (row[4].has_value())
            {
                user.avatar = row[4].value();
            }

            user.status = ParseStatus(row[5]);
            user.createTime =
                RequireValue(row[6], "users.create_time");
            user.updateTime =
                RequireValue(row[7], "users.update_time");

            return user;
        }

        UserProfile MakeUserProfile(
            const jobpilot::core::Result &result)
        {
            if (result.rowCount() != 1)
            {
                throw std::runtime_error(
                    "profile query returned an unexpected row count");
            }

            const auto &row = result.row(0);

            if (row.size() != 9)
            {
                throw std::runtime_error(
                    "profile query returned an unexpected column count");
            }

            UserProfile profile;
            profile.userId =
                ParseUint64(row[0], "profiles.user_id");

            if (row[1].has_value())
            {
                profile.targetPosition = row[1].value();
            }

            if (row[2].has_value())
            {
                profile.experience = row[2].value();
            }

            if (row[3].has_value())
            {
                profile.location = row[3].value();
            }

            if (row[4].has_value())
            {
                profile.salaryMin =
                    ParseUnsigned32(
                        row[4],
                        "profiles.salary_min");
            }

            if (row[5].has_value())
            {
                profile.salaryMax =
                    ParseUnsigned32(
                        row[5],
                        "profiles.salary_max");
            }

            if (row[6].has_value())
            {
                profile.summary = row[6].value();
            }

            profile.createTime =
                RequireValue(row[7], "profiles.create_time");
            profile.updateTime =
                RequireValue(row[8], "profiles.update_time");

            return profile;
        }

        UserSkill MakeUserSkill(
            const jobpilot::core::Result &result)
        {
            if (result.rowCount() != 1)
            {
                throw std::runtime_error(
                    "skill query returned an unexpected row count");
            }

            const auto &row = result.row(0);

            if (row.size() != 5)
            {
                throw std::runtime_error(
                    "skill query returned an unexpected column count");
            }

            UserSkill skill;
            skill.id =
                ParseUint64(row[0], "skills.id");
            skill.userId =
                ParseUint64(row[1], "skills.user_id");
            skill.name =
                RequireValue(row[2], "skills.name");
            skill.level =
                ParseInt32(row[3], "skills.level");
            skill.createTime =
                RequireValue(row[4], "skills.create_time");

            return skill;
        }

        std::optional<UserSkill> FindSkillByName(
            jobpilot::core::ConnectionPool *pool,
            std::uint64_t userId,
            const std::string &name)
        {
            const jobpilot::core::Result result =
                pool->Query(
                    "SELECT id, user_id, name, level, create_time "
                    "FROM skills "
                    "WHERE user_id = ? AND name = ? LIMIT 1",
                    {userId, name});

            if (result.rowCount() == 0)
            {
                return std::nullopt;
            }

            return MakeUserSkill(result);
        }
    } // namespace

    UserDao::UserDao(
        jobpilot::core::ConnectionPool &pool)
        : pool_(pool)
    {
    }

    std::optional<std::uint64_t> UserDao::CreateUser(
        const std::string &username,
        const std::string &email,
        const std::string &passwordHash)
    {
        const jobpilot::core::ExecutionResult result =
            pool_.Execute(
                "INSERT INTO users "
                "(username, email, password_hash) "
                "VALUES (?, ?, ?)",
                {username, email, passwordHash});

        if (result.affectedRows != 1 ||
            result.lastInsertId == 0)
        {
            return std::nullopt;
        }

        return result.lastInsertId;
    }

    std::optional<UserRecord> UserDao::FindByEmail(
        const std::string &email)
    {
        const jobpilot::core::Result result =
            pool_.Query(
                "SELECT id, username, email, password_hash, "
                "avatar, status, create_time, update_time "
                "FROM users WHERE email = ? LIMIT 1",
                {email});

        if (result.rowCount() == 0)
        {
            return std::nullopt;
        }

        return MakeUserRecord(result);
    }

    std::optional<UserRecord> UserDao::FindById(
        std::uint64_t userId)
    {
        const jobpilot::core::Result result =
            pool_.Query(
                "SELECT id, username, email, password_hash, "
                "avatar, status, create_time, update_time "
                "FROM users WHERE id = ? LIMIT 1",
                {userId});

        if (result.rowCount() == 0)
        {
            return std::nullopt;
        }

        return MakeUserRecord(result);
    }

    std::optional<UserProfile> UserDao::FindProfile(
        std::uint64_t userId)
    {
        const jobpilot::core::Result result =
            pool_.Query(
                "SELECT user_id, target_position, experience, "
                "location, salary_min, salary_max, summary, "
                "create_time, update_time "
                "FROM profiles WHERE user_id = ? LIMIT 1",
                {userId});

        if (result.rowCount() == 0)
        {
            return std::nullopt;
        }

        return MakeUserProfile(result);
    }

    bool UserDao::UpsertProfile(
        const UserProfile &profile)
    {
        pool_.Execute(
            "INSERT INTO profiles "
            "(user_id, target_position, experience, location, "
            "salary_min, salary_max, summary) "
            "VALUES (?, ?, ?, ?, ?, ?, ?) "
            "ON DUPLICATE KEY UPDATE "
            "target_position = VALUES(target_position), "
            "experience = VALUES(experience), "
            "location = VALUES(location), "
            "salary_min = VALUES(salary_min), "
            "salary_max = VALUES(salary_max), "
            "summary = VALUES(summary)",
            {profile.userId,
             OptionalStringParam(profile.targetPosition),
             OptionalStringParam(profile.experience),
             OptionalStringParam(profile.location),
             OptionalUnsignedParam(profile.salaryMin),
             OptionalUnsignedParam(profile.salaryMax),
             OptionalStringParam(profile.summary)});

        return true;
    }

    std::vector<UserSkill> UserDao::ListSkills(
        std::uint64_t userId)
    {
        const jobpilot::core::Result result =
            pool_.Query(
                "SELECT id, user_id, name, level, create_time "
                "FROM skills WHERE user_id = ? ORDER BY id ASC",
                {userId});

        std::vector<UserSkill> skills;
        skills.reserve(result.rowCount());

        for (std::size_t row = 0;
             row < result.rowCount();
             ++row)
        {
            const auto &values = result.row(row);

            if (values.size() != 5)
            {
                throw std::runtime_error(
                    "skill query returned an unexpected column count");
            }

            UserSkill skill;
            skill.id =
                ParseUint64(values[0], "skills.id");
            skill.userId =
                ParseUint64(values[1], "skills.user_id");
            skill.name =
                RequireValue(values[2], "skills.name");
            skill.level =
                ParseInt32(values[3], "skills.level");
            skill.createTime =
                RequireValue(values[4], "skills.create_time");

            skills.push_back(std::move(skill));
        }

        return skills;
    }

    std::optional<UserSkill> UserDao::UpsertSkill(
        std::uint64_t userId,
        const std::string &name,
        std::int32_t level)
    {
        pool_.Execute(
            "INSERT INTO skills (user_id, name, level) "
            "VALUES (?, ?, ?) "
            "ON DUPLICATE KEY UPDATE level = VALUES(level)",
            {userId, name, static_cast<std::int64_t>(level)});

        return FindSkillByName(
            &pool_,
            userId,
            name);
    }

    bool UserDao::DeleteSkill(
        std::uint64_t userId,
        std::uint64_t skillId)
    {
        pool_.Execute(
            "DELETE FROM skills WHERE id = ? AND user_id = ?",
            {skillId, userId});

        return true;
    }

} // namespace jobpilot::service
