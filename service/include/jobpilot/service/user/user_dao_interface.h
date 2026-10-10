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

        // 0015 A2 candidate data. All three are NULL = unknown and must never
        // be interpreted as a specific class. educationLevel reuses the
        // jobs.education_required enum (1不限/2大专/3本科/4硕士/5博士);
        // campusStatus is 1非应届/2应届/3在校; graduationYear is 1990..2100.
        std::optional<std::uint32_t> educationLevel;
        std::optional<std::uint32_t> graduationYear;
        std::optional<std::uint32_t> campusStatus;

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

    class IUserProfileReader
    {
    public:
        virtual ~IUserProfileReader() = default;

        virtual std::optional<UserProfile> FindProfile(
            std::uint64_t userId) = 0;

        virtual std::vector<UserSkill> ListSkills(
            std::uint64_t userId) = 0;
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
