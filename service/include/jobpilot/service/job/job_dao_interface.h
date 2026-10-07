#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace jobpilot::service
{
    struct JobSummary
    {
        std::uint64_t id = 0;
        std::string company;
        std::string title;
        std::optional<std::string> location;
        std::optional<std::uint32_t> salaryMin;
        std::optional<std::uint32_t> salaryMax;
        std::string salaryCurrency{"CNY"};
        std::string source;
        std::string createTime;
        std::string updateTime;
    };

    struct JobRequirement
    {
        std::string skill;
        std::int32_t level = 1;
    };

    struct JobDetail
    {
        JobSummary summary;
        std::optional<std::string> description;
        std::vector<JobRequirement> requirements;
        std::optional<std::string> sourceUrl;
        std::int32_t status = 1;
    };

    class IJobDao
    {
    public:
        virtual ~IJobDao() = default;

        virtual std::vector<JobSummary> SearchJobs(
            const std::optional<std::string> &keyword,
            const std::optional<std::string> &location,
            std::size_t limit,
            std::size_t offset) = 0;

        virtual std::size_t CountJobs(
            const std::optional<std::string> &keyword,
            const std::optional<std::string> &location) = 0;

        virtual std::optional<JobDetail> FindActiveJobById(
            std::uint64_t jobId) = 0;

        virtual std::vector<JobRequirement> ListRequirements(
            std::uint64_t jobId) = 0;
    };

} // namespace jobpilot::service
