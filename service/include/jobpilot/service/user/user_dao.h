#pragma once

#include "jobpilot/service/user/user_dao_interface.h"

namespace jobpilot::core
{
class ConnectionPool;
}

namespace jobpilot::service
{

class UserDao final : public IUserDao
{
public:
    explicit UserDao(
        jobpilot::core::ConnectionPool &pool);

    ~UserDao() override = default;

    UserDao(const UserDao &) = delete;
    UserDao &operator=(const UserDao &) = delete;

    std::optional<std::uint64_t> CreateUser(
        const std::string &username,
        const std::string &email,
        const std::string &passwordHash) override;

    std::optional<UserRecord> FindByEmail(
        const std::string &email) override;

    std::optional<UserRecord> FindById(
        std::uint64_t userId) override;

    std::optional<UserProfile> FindProfile(
        std::uint64_t userId) override;

    bool UpsertProfile(
        const UserProfile &profile) override;

    std::vector<UserSkill> ListSkills(
        std::uint64_t userId) override;

    std::optional<UserSkill> UpsertSkill(
        std::uint64_t userId,
        const std::string &name,
        std::int32_t level) override;

    bool DeleteSkill(
        std::uint64_t userId,
        std::uint64_t skillId) override;

private:
    jobpilot::core::ConnectionPool &pool_;
};

} // namespace jobpilot::service
