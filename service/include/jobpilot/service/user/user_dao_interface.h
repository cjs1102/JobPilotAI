#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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

    struct UserProfile
    {
        std::uint64_t userId = 0;
        std::optional<std::string> targetPosition;
        std::optional<std::string> experience;
        std::optional<std::string> location;
        std::optional<std::uint32_t> salaryMin;
        std::optional<std::uint32_t> salaryMax;
        std::optional<std::string> summary;
        std::string createTime;
        std::string updateTime;
    };

    struct UserSkill
    {
        std::uint64_t id = 0;
        std::uint64_t userId = 0;
        std::string name;
        std::int32_t level = 1;
        std::string createTime;
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

        virtual std::optional<UserProfile> FindProfile(
            std::uint64_t userId) = 0;

        virtual bool UpsertProfile(
            const UserProfile &profile) = 0;

        virtual std::vector<UserSkill> ListSkills(
            std::uint64_t userId) = 0;

        virtual std::optional<UserSkill> UpsertSkill(
            std::uint64_t userId,
            const std::string &name,
            std::int32_t level) = 0;

        virtual bool DeleteSkill(
            std::uint64_t userId,
            std::uint64_t skillId) = 0;
    };

} // namespace jobpilot::service
