// 0015A item 2: deterministic pre-ingest quality checks (pure function, no DB).
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

#include <gtest/gtest.h>

#include "jobpilot/service/job/job_quality.h"
#include "jobpilot/service/job/job_visibility.h"

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

    // Loads the shipped registry through the documented default path.
    // ctest runs from the build directory; a direct run may be from the repo
    // root or service/, so each depth is tried (returns an empty registry when
    // none resolves, and the caller asserts).
    PlatformNameRegistry LoadShippedRegistry()
    {
        const std::string candidates[] = {
            kDefaultPlatformNamesFile,
            "../" + std::string(kDefaultPlatformNamesFile),
            "../../../../" + std::string(kDefaultPlatformNamesFile),
            "../../../" + std::string(kDefaultPlatformNamesFile)};

        for (const std::string &candidate : candidates)
        {
            std::ifstream probe(candidate);

            if (probe.is_open())
            {
                std::string warning;
                return PlatformNameRegistry::Load(
                    candidate,
                    &warning);
            }
        }

        return PlatformNameRegistry::FromNames({});
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

    TEST(JobQualityTest, MissingCompanyGoesToReviewNotReject)
    {
        JobImportData job = MakeJob();
        job.company = "   ";

        const JobQualityReport report =
            EvaluateJobQuality(job, PlatformNameRegistry{});

        // 0015A option B: a missing employer is fixable, so it is queued for a
        // reviewer instead of rejecting the whole batch (nothing is lost).
        EXPECT_FALSE(report.rejected);
        EXPECT_EQ(report.reviewStatus, 1);
        EXPECT_TRUE(HasCode(report, "company_missing"));
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

    // 0015A option B (plan section 6.1 code 2/3): the verdict is asserted only
    // when the deterministic hostname mapping confirms the SAME platform; an
    // unknown or mismatching host degrades to advisory and the row stays
    // visible. Uses the shipped registry so the real mapping is exercised.
    TEST(JobQualityTest, PlatformEmployerWithConfirmingHostGoesToReview)
    {
        const PlatformNameRegistry platforms =
            LoadShippedRegistry();

        ASSERT_GT(platforms.HostMappingCount(), 0U);

        JobImportData job = MakeJob();
        job.company = "牛客网";
        job.sourceUrl = "https://www.nowcoder.com/job/123456";

        const JobQualityReport confirmed =
            EvaluateJobQuality(job, platforms);

        EXPECT_FALSE(confirmed.rejected);
        EXPECT_EQ(confirmed.reviewStatus, 1);
        EXPECT_TRUE(HasCode(confirmed, "employer_is_platform"));

        // Unknown host: no verdict, advisory, still visible.
        JobImportData unknownHost = job;
        unknownHost.sourceUrl = "https://example.invalid/job/123456";

        const JobQualityReport advisory =
            EvaluateJobQuality(unknownHost, platforms);

        EXPECT_EQ(advisory.reviewStatus, 0);
        EXPECT_FALSE(HasCode(advisory, "employer_is_platform"));
        EXPECT_TRUE(HasCode(advisory, "platform_host_unverified"));

        // A different platform's own host: still never guessed, advisory only.
        JobImportData otherHost = job;
        otherHost.sourceUrl = "https://www.zhipin.com/job/123456";

        const JobQualityReport mismatch =
            EvaluateJobQuality(otherHost, platforms);

        EXPECT_EQ(mismatch.reviewStatus, 0);
        EXPECT_FALSE(HasCode(mismatch, "employer_is_platform"));
        EXPECT_TRUE(HasCode(mismatch, "platform_host_unverified"));

        // A real employer is never queued by the platform rule.
        JobImportData realEmployer = job;
        realEmployer.company = "Acme Robotics";

        const JobQualityReport real =
            EvaluateJobQuality(realEmployer, platforms);

        EXPECT_EQ(real.reviewStatus, 0);
        EXPECT_FALSE(HasCode(real, "employer_is_platform"));
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
        const PlatformNameRegistry registry =
            LoadShippedRegistry();

        ASSERT_GT(registry.Size(), 0U)
            << "platform_names.yaml not found at "
            << kDefaultPlatformNamesFile;

        // The canonical normalized set (18 entries) — the Python parity test
        // asserts the identical set.
        const std::vector<std::string> expected = {
            "牛客", "实习僧", "国聘", "智联", "前程无忧",
            "中华英才", "应届生求职", "大街", "猎聘", "拉勾",
            "看准", "职友集", "nowcoder", "boss直聘", "51job",
            "lagou", "liepin", "zhaopin"};

        EXPECT_EQ(registry.Size(), expected.size());

        // The deterministic hostname mapping is part of the same single source
        // of truth (0015A option B).
        EXPECT_GT(registry.HostMappingCount(), 0U);

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

    // 0015A: the rollback switch is parsed in exactly one place so the list, the
    // detail, the application check and every other public read cannot disagree
    // about its value.
    TEST(JobQualityTest, PublicVisibilitySwitchParsesTheDocumentedValues)
    {
        constexpr const char *kSwitch = "JOB_PUBLIC_INCLUDE_NEEDS_REVIEW";

        ASSERT_EQ(::unsetenv(kSwitch), 0);
        EXPECT_FALSE(jobpilot::service::PublicVisibilityIncludesNeedsReview());

        for (const char *enabled : {"1", "true", "TRUE", "yes"})
        {
            ASSERT_EQ(::setenv(kSwitch, enabled, 1), 0);
            EXPECT_TRUE(jobpilot::service::PublicVisibilityIncludesNeedsReview())
                << "expected " << enabled << " to enable the switch";
        }

        for (const char *disabled : {"0", "false", "no", ""})
        {
            ASSERT_EQ(::setenv(kSwitch, disabled, 1), 0);
            EXPECT_FALSE(jobpilot::service::PublicVisibilityIncludesNeedsReview())
                << "expected '" << disabled << "' to keep clean-only reads";
        }

        ASSERT_EQ(::unsetenv(kSwitch), 0);
    }

} // namespace
