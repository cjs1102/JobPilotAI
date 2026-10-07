#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

#include "jobpilot/database/mysql_pool.h"
#include "jobpilot/service/resume/resume_dao.h"

namespace jobpilot::test
{
    struct ResumeVersionFixture
    {
        std::uint64_t resumeId = 0;
        std::uint64_t versionId = 0;
    };

    inline ResumeVersionFixture InsertResumeVersion(
        jobpilot::core::ConnectionPool *pool,
        std::uint64_t userId,
        const std::string &title,
        const std::string &content)
    {
        if (pool == nullptr || userId == 0)
        {
            throw std::invalid_argument(
                "resume fixture requires pool and user");
        }

        jobpilot::service::ResumeDao dao(*pool);
        auto transaction =
            pool->BeginTransaction();

        const std::optional<std::uint64_t> resumeId =
            dao.CreateResume(
                transaction,
                userId,
                title);

        if (!resumeId.has_value())
        {
            throw std::runtime_error(
                "failed to create resume fixture");
        }

        const std::optional<std::uint64_t> versionId =
            dao.CreateVersion(
                transaction,
                resumeId.value(),
                1,
                std::nullopt,
                content);

        if (!versionId.has_value())
        {
            throw std::runtime_error(
                "failed to create resume version fixture");
        }

        transaction.Commit();

        return ResumeVersionFixture{
            resumeId.value(),
            versionId.value()};
    }
} // namespace jobpilot::test
