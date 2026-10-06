#include "jobpilot/service/user/user_dao.h"

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
                    "users." + field + " must not be NULL");
            }

            return value.value();
        }

        std::uint64_t ParseUserId(
            const jobpilot::core::Result::Value &value)
        {
            const std::string text =
                RequireValue(value, "id");

            std::size_t parsed = 0;

            const unsigned long long userId =
                std::stoull(text, &parsed);

            if (parsed != text.size())
            {
                throw std::runtime_error(
                    "users.id contains invalid characters");
            }

            return static_cast<std::uint64_t>(
                userId);
        }

        std::int32_t ParseStatus(
            const jobpilot::core::Result::Value &value)
        {
            const std::string text =
                RequireValue(value, "status");

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
            user.id = ParseUserId(row[0]);
            user.username = RequireValue(row[1], "username");
            user.email = RequireValue(row[2], "email");
            user.passwordHash =
                RequireValue(row[3], "password_hash");

            if (row[4].has_value())
            {
                user.avatar = row[4].value();
            }

            user.status = ParseStatus(row[5]);
            user.createTime =
                RequireValue(row[6], "create_time");
            user.updateTime =
                RequireValue(row[7], "update_time");

            return user;
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

} // namespace jobpilot::service
