// 0015A item 2: deterministic pre-ingest quality checks (pure function, no DB).
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

#include <gtest/gtest.h>

#include "jobpilot/service/job/job_quality.h"

namespace
{
    using jobpilot::service::EvaluateJobQuality;
    using jobpilot::service::JobImportData;
    using jobpilot::service::JobQualityOutcome;
    using jobpilot::service::JobQualityReport;
    using jobpilot::service::kDefaultPlatformNamesFile;
    using jobpilot::service::kMaxDescriptionBytes;
    using jobpilot::service::kPlatformNamesFileEnv;
    using jobpilot::service::MarkDuplicateContentInBatch;
    using jobpilot::service::PlatformNameRegistry;

    JobImportData MakeJob()
    {
        JobImportData job;
        job.company = "Acme Robotics";
        job.title = "C++ backend engineer";
        job.source = "manual";
        job.sourceUrl = "https://jobs.example.invalid/job/123456";
        return job;
    }

    bool HasCode(
        const JobQualityReport &report,
        const std::string &code)
    {
        for (const auto &finding : report.findings)
        {
            if (finding.code == code)
            {
                return true;
            }
        }

        return false;
    }

    TEST(JobQualityTest, CleanJobHasNoFindingsAndStaysVisible)
    {
        const JobQualityReport report =
            EvaluateJobQuality(
                MakeJob(),
                PlatformNameRegistry::FromNames({"牛客"}));

        EXPECT_FALSE(report.rejected);
        EXPECT_EQ(report.reviewStatus, 0);
        EXPECT_TRUE(report.findings.empty());
    }

    TEST(JobQualityTest, MissingCompanyIsRejectedWithDiagnostic)
    {
        JobImportData job = MakeJob();
        job.company = "   ";

        const JobQualityReport report =
            EvaluateJobQuality(job, PlatformNameRegistry{});

        EXPECT_TRUE(report.rejected);
        EXPECT_EQ(report.diagnosticCode, "company_missing");
    }

    TEST(JobQualityTest, NonHttpSourceUrlIsRejected)
    {
        JobImportData job = MakeJob();
        job.sourceUrl = "ftp://example.invalid/job/1";

        const JobQualityReport report =
            EvaluateJobQuality(job, PlatformNameRegistry{});

        EXPECT_TRUE(report.rejected);
        EXPECT_EQ(report.diagnosticCode, "source_url_unusable");
    }

    TEST(JobQualityTest, DescriptionOver48KiBIsRejectedNotTruncated)
    {
        JobImportData job = MakeJob();
        job.description = std::string(kMaxDescriptionBytes, 'x');

        const JobQualityReport atCap =
            EvaluateJobQuality(job, PlatformNameRegistry{});
        EXPECT_FALSE(atCap.rejected);

        job.description = std::string(kMaxDescriptionBytes + 1, 'x');

        const JobQualityReport overCap =
            EvaluateJobQuality(job, PlatformNameRegistry{});
        EXPECT_TRUE(overCap.rejected);
        EXPECT_EQ(overCap.diagnosticCode, "description_too_large");
    }

    TEST(JobQualityTest, RootOrListUrlGoesToReviewButStaysStored)
    {
        JobImportData job = MakeJob();
        job.sourceUrl = "https://jobs.example.invalid/";

        const JobQualityReport report =
            EvaluateJobQuality(job, PlatformNameRegistry{});

        EXPECT_FALSE(report.rejected);
        EXPECT_EQ(report.reviewStatus, 1);
        EXPECT_TRUE(HasCode(report, "source_url_not_detail"));
    }

    TEST(JobQualityTest, PlatformNameAsEmployerGoesToReview)
    {
        JobImportData job = MakeJob();
        job.company = "牛客网";

        const JobQualityReport report =
            EvaluateJobQuality(
                job,
                PlatformNameRegistry::FromNames({"牛客"}));

        EXPECT_EQ(report.reviewStatus, 1);
        EXPECT_TRUE(HasCode(report, "employer_is_platform"));
    }

    TEST(JobQualityTest, UnknownPlatformNameStaysCleanFailOpen)
    {
        JobImportData job = MakeJob();
        job.company = "Some Unknown Board";

        const JobQualityReport report =
            EvaluateJobQuality(
                job,
                PlatformNameRegistry::FromNames({"牛客"}));

        EXPECT_EQ(report.reviewStatus, 0);
        EXPECT_FALSE(HasCode(report, "employer_is_platform"));
    }

    TEST(JobQualityTest, TitleEqualsCompanyGoesToReview)
    {
        JobImportData job = MakeJob();
        job.title = "Acme Robotics";

        const JobQualityReport report =
            EvaluateJobQuality(job, PlatformNameRegistry{});

        EXPECT_EQ(report.reviewStatus, 1);
        EXPECT_TRUE(HasCode(report, "title_equals_company"));
    }

    TEST(JobQualityTest, PollutedDescriptionNeedsTheConjunctiveThreshold)
    {
        JobImportData job = MakeJob();
        job.description =
            "岗位职责：写代码 推荐岗位 相关职位 热门职位 猜你喜欢 浏览更多";
        // 6 distinct markers, marker share above 20% -> needs_review.
        const JobQualityReport polluted =
            EvaluateJobQuality(job, PlatformNameRegistry{});
        EXPECT_EQ(polluted.reviewStatus, 1);
        EXPECT_TRUE(HasCode(polluted, "description_polluted"));

        // A single stray marker is advisory only and stays visible.
        JobImportData minor = MakeJob();
        minor.description =
            "岗位职责：写代码，另有推荐岗位若干，其余为正文内容描述。";
        const JobQualityReport advisory =
            EvaluateJobQuality(minor, PlatformNameRegistry{});
        EXPECT_EQ(advisory.reviewStatus, 0);
        EXPECT_TRUE(HasCode(advisory, "description_marker_minor"));
    }

    TEST(JobQualityTest, DateOnlyDeadlineWithWrongInstantNeedsReview)
    {
        JobImportData job = MakeJob();
        job.deadline = "2026-10-31 10:30:00";
        job.deadlineDateOnly = true;

        const JobQualityReport report =
            EvaluateJobQuality(job, PlatformNameRegistry{});

        EXPECT_EQ(report.reviewStatus, 1);
        EXPECT_TRUE(HasCode(report, "deadline_flag_inconsistent"));

        JobImportData correct = MakeJob();
        correct.deadline = "2026-10-31 04:00:00";
        correct.deadlineDateOnly = true;

        EXPECT_EQ(
            EvaluateJobQuality(correct, PlatformNameRegistry{}).reviewStatus,
            0);
    }

    TEST(JobQualityTest, DuplicateContentInBatchFlagsBothItems)
    {
        JobImportData first = MakeJob();
        JobImportData second = MakeJob();
        second.sourceUrl = "https://other.example.invalid/job/999999";

        const std::vector<JobImportData> jobs = {first, second};
        std::vector<JobQualityReport> reports(2);

        MarkDuplicateContentInBatch(jobs, &reports);

        EXPECT_EQ(reports[0].reviewStatus, 1);
        EXPECT_EQ(reports[1].reviewStatus, 1);
        EXPECT_TRUE(HasCode(reports[0], "duplicate_content_in_batch"));
        EXPECT_TRUE(HasCode(reports[1], "duplicate_content_in_batch"));
    }

    TEST(JobQualityTest, EvaluatorIsDeterministic)
    {
        const JobImportData job = MakeJob();
        const PlatformNameRegistry platforms =
            PlatformNameRegistry::FromNames({"牛客"});

        const JobQualityReport first =
            EvaluateJobQuality(job, platforms);
        const JobQualityReport second =
            EvaluateJobQuality(job, platforms);

        EXPECT_EQ(first.reviewStatus, second.reviewStatus);
        EXPECT_EQ(first.findings.size(), second.findings.size());
    }

    TEST(JobQualityTest, RegistryNormalisesSuffixesAndCase)
    {
        const PlatformNameRegistry registry =
            PlatformNameRegistry::FromNames({"牛客"});

        EXPECT_TRUE(registry.Contains("牛客"));
        EXPECT_TRUE(registry.Contains(" 牛客网 "));
        EXPECT_EQ(
            PlatformNameRegistry::Normalize("BOSS直聘"),
            PlatformNameRegistry::Normalize("boss直聘"));
        EXPECT_FALSE(registry.Contains("Acme"));
    }

    // Deterministic cross-language parity basis: the Python registry test
    // asserts this same table and this same normalized set, so both sides
    // demonstrably produce identical output from one file.
    TEST(JobQualityTest, NormalizeFollowsTheCanonicalTable)
    {
        struct Case
        {
            const char *input;
            const char *normalized;
        };

        const Case cases[] = {
            // Chinese names and whitespace trimming.
            {" 牛客 ", "牛客"},
            {"牛客", "牛客"},
            // Suffix stripping, including a chained/combined suffix.
            {"牛客网", "牛客"},
            {"牛客官网", "牛客"},
            {"牛客招聘", "牛客"},
            {"牛客招聘网", "牛客"},
            {"牛客人才网", "牛客"},
            {"牛客网招聘网", "牛客"},
            {"智联招聘", "智联"},
            {"中华英才网", "中华英才"},
            {"应届生求职网", "应届生求职"},
            {"大街网", "大街"},
            // ASCII case folding + trimming.
            {"BOSS直聘", "boss直聘"},
            {"boss直聘", "boss直聘"},
            {"  51JOB  ", "51job"},
            {"NOWCODER", "nowcoder"},
            // No suffix is invented where none exists.
            {"前程无忧", "前程无忧"},
            {"猎聘", "猎聘"}};

        for (const Case &item : cases)
        {
            EXPECT_EQ(
                PlatformNameRegistry::Normalize(item.input),
                item.normalized)
                << "input: " << item.input;
        }
    }

    TEST(JobQualityTest, ShippedRegistryNormalizesToTheCanonicalSet)
    {
        // ctest runs from the build directory; a direct run may be from the
        // repo root or service/. Try the documented default at each depth.
        const std::string candidates[] = {
            kDefaultPlatformNamesFile,
            "../" + std::string(kDefaultPlatformNamesFile),
            "../../../../" + std::string(kDefaultPlatformNamesFile),
            "../../../" + std::string(kDefaultPlatformNamesFile)};

        std::string loadedPath;

        for (const std::string &candidate : candidates)
        {
            std::ifstream probe(candidate);

            if (probe.is_open())
            {
                loadedPath = candidate;
                break;
            }
        }

        ASSERT_FALSE(loadedPath.empty())
            << "platform_names.yaml not found at "
            << kDefaultPlatformNamesFile;

        std::string warning;
        const PlatformNameRegistry registry =
            PlatformNameRegistry::Load(loadedPath, &warning);

        EXPECT_TRUE(warning.empty());

        // The canonical normalized set (18 entries) — the Python parity test
        // asserts the identical set.
        const std::vector<std::string> expected = {
            "牛客", "实习僧", "国聘", "智联", "前程无忧",
            "中华英才", "应届生求职", "大街", "猎聘", "拉勾",
            "看准", "职友集", "nowcoder", "boss直聘", "51job",
            "lagou", "liepin", "zhaopin"};

        EXPECT_EQ(registry.Size(), expected.size());

        for (const std::string &name : expected)
        {
            EXPECT_TRUE(registry.Contains(name)) << name;
        }

        // Raw file spellings resolve to the same entries.
        for (const char *raw :
             {"牛客网", "智联招聘", "中华英才网",
              "应届生求职网", "大街网", "BOSS直聘"})
        {
            EXPECT_TRUE(registry.Contains(raw)) << raw;
        }

        // Fail-open: a name outside the registry never matches.
        EXPECT_FALSE(registry.Contains("Acme Robotics"));
        EXPECT_FALSE(registry.Contains(""));
    }

    // 0015A item 2: the registry path comes from
    // JOB_QUALITY_PLATFORM_NAMES_FILE, falling back to the repo-relative
    // default, and the file stays the single source of truth.
    TEST(JobQualityTest, RegistryPathComesFromTheEnvironment)
    {
        const std::string path =
            "/tmp/jobpilot-platform-names-" +
            std::to_string(getpid()) + ".yaml";

        {
            std::ofstream output(path);
            ASSERT_TRUE(output);
            output << "version: 1\n"
                      "platform_names:\n"
                      "  - Acme Recruitment\n";
        }

        ASSERT_EQ(
            ::setenv(kPlatformNamesFileEnv, path.c_str(), 1),
            0);

        std::string warning;
        const PlatformNameRegistry registry =
            PlatformNameRegistry::LoadFromEnvironment(&warning);

        EXPECT_TRUE(warning.empty());
        EXPECT_EQ(registry.Size(), 1U);
        EXPECT_TRUE(registry.Contains("Acme Recruitment"));
        EXPECT_TRUE(registry.Contains(" acme recruitment "));

        ASSERT_EQ(::unsetenv(kPlatformNamesFileEnv), 0);
        std::remove(path.c_str());

        // Fail-open: an unreadable path yields an empty registry and a warning.
        std::string missingWarning;
        const PlatformNameRegistry missing =
            PlatformNameRegistry::Load(
                "/nonexistent/platform_names.yaml",
                &missingWarning);

        EXPECT_EQ(missing.Size(), 0U);
        EXPECT_FALSE(missingWarning.empty());
    }
} // namespace
