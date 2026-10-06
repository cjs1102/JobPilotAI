#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace jobpilot::service
{

    struct UserRecord
    {
        std::uint64_t id = 0;
        std::string username;
        std::string email;
        std::string passwordHash;
        std::string avatar;
        std::int32_t status = 1;
        std::string createTime;
        std::string updateTime;
    };

    class IUserDao
    {
    public:
        virtual ~IUserDao() = default;

        virtual std::optional<std::uint64_t> CreateUser(
            const std::string &username,
            const std::string &email,
            const std::string &passwordHash) = 0;

        virtual std::optional<UserRecord> FindByEmail(
            const std::string &email) = 0;

        virtual std::optional<UserRecord> FindById(
            std::uint64_t userId) = 0;
    };

} // namespace jobpilot::service
