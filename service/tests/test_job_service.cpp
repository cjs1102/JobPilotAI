#include "jobpilot/service/job/job_service.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace jobpilot::service
{
    namespace
    {
        class FakeJobDao final : public IJobDao
        {
        public:
            std::vector<JobSummary> SearchJobs(
                const JobSearchFilter &filter,
                std::size_t limit,
                std::size_t offset) override
            {
                if (throwOnSearch)
                {
                    throw std::runtime_error(
                        "forced search failure");
                }

                capturedKeyword = filter.keyword;
                capturedLocation = filter.location;
                capturedSalaryMin = filter.salaryMin;
                capturedSalaryMax = filter.salaryMax;
                capturedSkill = filter.skill;
                capturedLimit = limit;
                capturedOffset = offset;
                return searchResults;
            }

            std::size_t CountJobs(
                const JobSearchFilter &filter) override
            {
                if (throwOnCount)
                {
                    throw std::runtime_error(
                        "forced count failure");
                }

                capturedCountKeyword = filter.keyword;
                capturedCountLocation = filter.location;
                return total;
            }

            std::optional<JobDetail> FindActiveJobById(
                std::uint64_t jobId) override
            {
                if (throwOnDetail)
                {
                    throw std::runtime_error(
                        "forced detail failure");
                }

                capturedJobId = jobId;
                return detail;
            }

            bool ActiveJobExists(
                jobpilot::core::ConnectionPool::Transaction &,
                std::uint64_t) override
            {
                return activeJobExists;
            }

            std::vector<JobRequirement> ListRequirements(
                std::uint64_t jobId) override
            {
                capturedRequirementJobId = jobId;
                return requirements;
            }

            std::vector<JobSummary> searchResults;
            std::optional<JobDetail> detail;
            std::vector<JobRequirement> requirements;
            std::size_t total = 0;
            bool throwOnSearch = false;
            bool throwOnCount = false;
            bool throwOnDetail = false;
            bool activeJobExists = true;

            std::optional<std::string> capturedKeyword;
            std::optional<std::string> capturedLocation;
            std::optional<std::string> capturedCountKeyword;
            std::optional<std::string> capturedCountLocation;
            std::optional<std::uint32_t> capturedSalaryMin;
            std::optional<std::uint32_t> capturedSalaryMax;
            std::optional<std::string> capturedSkill;
            std::size_t capturedLimit = 0;
            std::size_t capturedOffset = 0;
            std::uint64_t capturedJobId = 0;
            std::uint64_t capturedRequirementJobId = 0;
        };
    } // namespace

    TEST(JobServiceTest, TrimsFiltersAndBuildsPagination)
    {
        FakeJobDao dao;
        dao.total = 1;
        JobSummary summary;
        summary.id = 7;
        summary.company = "Acme";
        summary.title = "C++";
        summary.source = "manual";
        dao.searchResults.push_back(summary);

        JobService service(dao);
        const ServiceResult<JobPage> result =
            service.SearchJobs(
                JobSearchFilter{
                    "  C++  ",
                    "  Shenzhen ",
                    std::nullopt,
                    std::nullopt,
                    std::nullopt},
                2,
                20);

        ASSERT_EQ(result.code, ErrorCode::kSuccess);
        EXPECT_EQ(result.value.total, 1U);
        EXPECT_EQ(result.value.page, 2U);
        EXPECT_EQ(result.value.pageSize, 20U);
        EXPECT_EQ(result.value.list.size(), 1U);
        EXPECT_EQ(dao.capturedKeyword.value(), "C++");
        EXPECT_EQ(dao.capturedLocation.value(), "Shenzhen");
        EXPECT_EQ(dao.capturedLimit, 20U);
        EXPECT_EQ(dao.capturedOffset, 20U);
    }

    TEST(JobServiceTest, EmptyFiltersBecomeNullopt)
    {
        FakeJobDao dao;
        JobService service(dao);

        const ServiceResult<JobPage> result =
            service.SearchJobs(
                JobSearchFilter{
                    "   ",
                    "",
                    std::nullopt,
                    std::nullopt,
                    std::nullopt},
                1,
                20);

        ASSERT_EQ(result.code, ErrorCode::kSuccess);
        EXPECT_FALSE(
            dao.capturedKeyword.has_value());
        EXPECT_FALSE(
            dao.capturedLocation.has_value());
    }

    TEST(JobServiceTest, NormalizesExtendedFilters)
    {
        FakeJobDao dao;
        JobService service(dao);

        const ServiceResult<JobPage> result =
            service.SearchJobs(
                JobSearchFilter{
                    std::nullopt,
                    std::nullopt,
                    std::uint32_t(20),
                    std::uint32_t(35),
                    std::string("  C++  ")},
                1,
                20);

        ASSERT_EQ(result.code, ErrorCode::kSuccess);
        ASSERT_TRUE(dao.capturedSalaryMin.has_value());
        ASSERT_TRUE(dao.capturedSalaryMax.has_value());
        ASSERT_TRUE(dao.capturedSkill.has_value());
        EXPECT_EQ(dao.capturedSalaryMin.value(), 20U);
        EXPECT_EQ(dao.capturedSalaryMax.value(), 35U);
        EXPECT_EQ(dao.capturedSkill.value(), "C++");

        EXPECT_EQ(
            service.SearchJobs(
                       JobSearchFilter{
                           std::nullopt,
                           std::nullopt,
                           std::uint32_t(40),
                           std::uint32_t(20),
                           std::nullopt},
                       1,
                       20)
                .code,
            ErrorCode::kInvalidParam);
    }

    TEST(JobServiceTest, RejectsInvalidPagination)
    {
        FakeJobDao dao;
        JobService service(dao);

        EXPECT_EQ(
            service.SearchJobs({}, 0, 20).code,
            ErrorCode::kInvalidParam);
        EXPECT_EQ(
            service.SearchJobs({}, 1, 0).code,
            ErrorCode::kInvalidParam);
        EXPECT_EQ(
            service.SearchJobs({}, 1, 101).code,
            ErrorCode::kInvalidParam);

        const std::size_t max =
            std::numeric_limits<std::size_t>::max();
        EXPECT_EQ(
            service.SearchJobs(
                       {},
                       max,
                       2)
                .code,
            ErrorCode::kInvalidParam);
    }

    TEST(JobServiceTest, LoadsRequirementsAndMapsMissingJob)
    {
        FakeJobDao dao;
        JobDetail detail;
        detail.summary.id = 42;
        detail.summary.title = "Backend";
        detail.summary.company = "Acme";
        detail.summary.source = "manual";
        dao.detail = detail;
        dao.requirements = {
            JobRequirement{"C++", 3},
            JobRequirement{"Redis", 2}};

        JobService service(dao);

        const ServiceResult<JobDetail> found =
            service.GetJob(42);

        ASSERT_EQ(found.code, ErrorCode::kSuccess);
        ASSERT_EQ(found.value.requirements.size(), 2U);
        EXPECT_EQ(
            found.value.requirements[0].skill,
            "C++");
        EXPECT_EQ(dao.capturedJobId, 42U);
        EXPECT_EQ(dao.capturedRequirementJobId, 42U);

        dao.detail.reset();
        const ServiceResult<JobDetail> missing =
            service.GetJob(99);

        EXPECT_EQ(
            missing.code,
            ErrorCode::kJobNotFound);
    }

    TEST(JobServiceTest, MapsDaoFailures)
    {
        FakeJobDao dao;
        dao.throwOnCount = true;
        JobService service(dao);

        EXPECT_EQ(
            service.SearchJobs({}, 1, 20).code,
            ErrorCode::kDatabaseError);

        dao.throwOnCount = false;
        dao.throwOnDetail = true;
        EXPECT_EQ(
            service.GetJob(1).code,
            ErrorCode::kDatabaseError);
    }
} // namespace jobpilot::service
