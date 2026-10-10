#include "jobpilot/service/job/source_observation.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace jobpilot::service
{
    namespace
    {
        SourceObservationJob MakeJob(
            const std::string &source,
            const std::string &sourceUrl)
        {
            SourceObservationJob item;
            item.job.company = "Example";
            item.job.title = "Backend Engineer";
            item.job.source = source;
            item.job.sourceUrl = sourceUrl;
            return item;
        }

        SourceObservation MakeObservation()
        {
            SourceObservation observation;
            observation.source = "greenhouse";
            observation.sourceScope = "example-company";
            observation.complete = true;
            observation.observedAt = "2026-10-08 12:00:00";
            observation.jobs.push_back(
                MakeJob(
                    "greenhouse",
                    "https://boards.greenhouse.io/example/jobs/1"));
            return observation;
        }

        SourceObservation MakeObservationWithJobs(
            std::size_t count)
        {
            SourceObservation observation = MakeObservation();
            observation.jobs.clear();

            for (std::size_t index = 0; index < count; ++index)
            {
                observation.jobs.push_back(
                    MakeJob(
                        "greenhouse",
                        "https://boards.greenhouse.io/example/jobs/" +
                            std::to_string(index)));
            }

            return observation;
        }
    } // namespace

    TEST(SourceObservationValidation, AcceptsCompleteObservationWithJobs)
    {
        EXPECT_TRUE(ValidateSourceObservation(MakeObservation()));
    }

    TEST(SourceObservationValidation, AcceptsCompleteEmptyBoard)
    {
        SourceObservation observation = MakeObservation();
        observation.jobs.clear();

        EXPECT_TRUE(ValidateSourceObservation(observation));
    }

    TEST(SourceObservationValidation, RejectsIncompleteEmptyBoard)
    {
        SourceObservation observation = MakeObservation();
        observation.complete = false;
        observation.jobs.clear();

        EXPECT_FALSE(ValidateSourceObservation(observation));
    }

    TEST(SourceObservationValidation, AcceptsIncompletePartialBatch)
    {
        SourceObservation observation = MakeObservation();
        observation.complete = false;

        EXPECT_TRUE(ValidateSourceObservation(observation));
    }

    TEST(SourceObservationSources, AcceptsOnlyConfiguredProviders)
    {
        EXPECT_TRUE(IsSupportedObservationSource("greenhouse"));
        EXPECT_TRUE(IsSupportedObservationSource("lever"));
        EXPECT_TRUE(IsSupportedObservationSource("ashby"));
        EXPECT_FALSE(IsSupportedObservationSource(""));
        EXPECT_FALSE(IsSupportedObservationSource("manual"));
        EXPECT_FALSE(IsSupportedObservationSource("import"));
        EXPECT_FALSE(IsSupportedObservationSource("crawler"));
        EXPECT_FALSE(IsSupportedObservationSource("Greenhouse"));
    }

    TEST(SourceObservationValidation, AcceptsAllSupportedSources)
    {
        for (const char *source :
             {"greenhouse", "lever", "ashby"})
        {
            SourceObservation observation = MakeObservation();
            observation.source = source;
            observation.jobs[0].job.source = source;

            EXPECT_TRUE(ValidateSourceObservation(observation))
                << source;
        }
    }

    TEST(SourceObservationValidation, RejectsUnsupportedOrEmptySource)
    {
        for (const char *source :
             {"", "manual", "import", "crawler", "workday"})
        {
            SourceObservation observation = MakeObservation();
            observation.source = source;

            EXPECT_FALSE(ValidateSourceObservation(observation))
                << source;
        }
    }

    TEST(SourceObservationSources, KeepsTheAllowlistClosedUntilASourceIsVerified)
    {
        // 0014 Step 1: no China identifier may be pre-registered before the
        // per-source verification (Step 2) and registration (Step 3) land, so
        // an unknown string can never reach the observation pipeline. When a
        // later step widens the allowlist, this list must be revised item by
        // item instead of being relaxed wholesale.
        for (const char *source :
             {"moka", "mokahr", "beisen", "echeng", "campus_web",
              "iguopin", "workday", "cn_campus", "Greenhouse",
              "greenhouse ", " greenhouse", "greenhouse\n", "greenHOUSE"})
        {
            EXPECT_FALSE(IsSupportedObservationSource(source))
                << source;
        }
    }

    TEST(SourceObservationValidation, RequiresEveryJobToCarryTheObservationSource)
    {
        // Same-source semantics (0014 Step 1 regression): a batch belongs to
        // exactly one source, and every job in it must repeat that identity.
        for (const char *source :
             {"greenhouse", "lever", "ashby"})
        {
            SourceObservation matching = MakeObservation();
            matching.source = source;
            matching.jobs[0].job.source = source;

            EXPECT_TRUE(ValidateSourceObservation(matching))
                << source;

            SourceObservation mismatched = MakeObservation();
            mismatched.source = source;
            mismatched.jobs[0].job.source =
                std::string(source) == "lever" ? "ashby" : "lever";

            EXPECT_FALSE(ValidateSourceObservation(mismatched))
                << source;
        }
    }

    TEST(SourceObservationValidation, RejectsSourceTooLong)
    {
        SourceObservation observation = MakeObservation();
        observation.source = std::string(33, 'g');

        EXPECT_FALSE(ValidateSourceObservation(observation));
    }

    TEST(SourceObservationValidation, EnforcesSourceScopeBounds)
    {
        SourceObservation empty = MakeObservation();
        empty.sourceScope.clear();
        EXPECT_FALSE(ValidateSourceObservation(empty));

        SourceObservation tooLong = MakeObservation();
        tooLong.sourceScope = std::string(129, 'b');
        EXPECT_FALSE(ValidateSourceObservation(tooLong));

        SourceObservation limit = MakeObservation();
        limit.sourceScope = std::string(128, 'b');
        EXPECT_TRUE(ValidateSourceObservation(limit));
    }

    TEST(DbTimestamp, ValidatesFormatAndRanges)
    {
        EXPECT_TRUE(IsValidDbTimestamp("2026-10-08 12:00:00"));
        EXPECT_TRUE(IsValidDbTimestamp("2024-02-29 23:59:59"));
        EXPECT_TRUE(IsValidDbTimestamp("1000-01-01 00:00:00"));
        EXPECT_TRUE(IsValidDbTimestamp("9999-12-31 23:59:59"));
        EXPECT_FALSE(IsValidDbTimestamp(""));
        EXPECT_FALSE(IsValidDbTimestamp("2026-10-08"));
        EXPECT_FALSE(IsValidDbTimestamp("12:00:00"));
        EXPECT_FALSE(IsValidDbTimestamp("2026-10-08T12:00:00"));
        EXPECT_FALSE(IsValidDbTimestamp("2026/10/08 12:00:00"));
        EXPECT_FALSE(IsValidDbTimestamp("2026-02-29 00:00:00"));
        EXPECT_FALSE(IsValidDbTimestamp("2026-04-31 00:00:00"));
        EXPECT_FALSE(IsValidDbTimestamp("2026-13-01 00:00:00"));
        EXPECT_FALSE(IsValidDbTimestamp("2026-10-08 24:00:00"));
        EXPECT_FALSE(IsValidDbTimestamp("2026-10-08 12:60:00"));
        EXPECT_FALSE(IsValidDbTimestamp("2026-10-08 12:00:60"));
        EXPECT_FALSE(IsValidDbTimestamp("2026-10-08 12:00:0a"));
        EXPECT_FALSE(IsValidDbTimestamp("2026-10-08 12:00:00.000"));
        EXPECT_FALSE(IsValidDbTimestamp("0999-10-08 12:00:00"));
    }

    TEST(SourceObservationValidation, RejectsMalformedObservedAt)
    {
        SourceObservation observation = MakeObservation();
        observation.observedAt = "2026-10-08T12:00:00";

        EXPECT_FALSE(ValidateSourceObservation(observation));
    }

    TEST(SourceObservationValidation, RejectsEmptyObservedAt)
    {
        SourceObservation observation = MakeObservation();
        observation.observedAt.clear();

        EXPECT_FALSE(ValidateSourceObservation(observation));
    }

    TEST(SourceObservationValidation, EnforcesBatchLimits)
    {
        SourceObservation completeLimit =
            MakeObservationWithJobs(kMaxObservationBatchSize);
        EXPECT_TRUE(ValidateSourceObservation(completeLimit));

        SourceObservation completeOver =
            MakeObservationWithJobs(kMaxObservationBatchSize + 1);
        EXPECT_FALSE(ValidateSourceObservation(completeOver));

        SourceObservation partialLimit =
            MakeObservationWithJobs(kMaxObservationBatchSize);
        partialLimit.complete = false;
        EXPECT_TRUE(ValidateSourceObservation(partialLimit));

        SourceObservation partialOver =
            MakeObservationWithJobs(kMaxObservationBatchSize + 1);
        partialOver.complete = false;
        EXPECT_FALSE(ValidateSourceObservation(partialOver));
    }

    TEST(SourceObservationValidation, RejectsJobSourceMismatch)
    {
        SourceObservation observation = MakeObservation();
        observation.jobs[0].job.source = "lever";

        EXPECT_FALSE(ValidateSourceObservation(observation));
    }

    TEST(SourceObservationValidation, RejectsDuplicateSourceUrl)
    {
        SourceObservation exact = MakeObservation();
        exact.jobs.push_back(
            MakeJob(
                "greenhouse",
                "https://boards.greenhouse.io/example/jobs/1"));
        EXPECT_FALSE(ValidateSourceObservation(exact));

        SourceObservation foldedCase = MakeObservation();
        foldedCase.jobs.push_back(
            MakeJob(
                "greenhouse",
                "HTTPS://BOARDS.GREENHOUSE.IO/example/jobs/1"));
        EXPECT_FALSE(ValidateSourceObservation(foldedCase));

        SourceObservation distinct = MakeObservation();
        distinct.jobs.push_back(
            MakeJob(
                "greenhouse",
                "https://boards.greenhouse.io/example/jobs/2"));
        EXPECT_TRUE(ValidateSourceObservation(distinct));
    }

    TEST(SourceObservationValidation, RejectsInvalidJobFields)
    {
        SourceObservation noCompany = MakeObservation();
        noCompany.jobs[0].job.company.clear();
        EXPECT_FALSE(ValidateSourceObservation(noCompany));

        SourceObservation noTitle = MakeObservation();
        noTitle.jobs[0].job.title.clear();
        EXPECT_FALSE(ValidateSourceObservation(noTitle));

        SourceObservation noSourceUrl = MakeObservation();
        noSourceUrl.jobs[0].job.sourceUrl.clear();
        EXPECT_FALSE(ValidateSourceObservation(noSourceUrl));

        SourceObservation noCurrency = MakeObservation();
        noCurrency.jobs[0].job.salaryCurrency.clear();
        EXPECT_FALSE(ValidateSourceObservation(noCurrency));

        SourceObservation longLocation = MakeObservation();
        longLocation.jobs[0].job.location = std::string(65, 'x');
        EXPECT_FALSE(ValidateSourceObservation(longLocation));

        SourceObservation invertedSalary = MakeObservation();
        invertedSalary.jobs[0].job.salaryMin = 30;
        invertedSalary.jobs[0].job.salaryMax = 20;
        EXPECT_FALSE(ValidateSourceObservation(invertedSalary));

        SourceObservation equalSalary = MakeObservation();
        equalSalary.jobs[0].job.salaryMin = 20;
        equalSalary.jobs[0].job.salaryMax = 20;
        EXPECT_TRUE(ValidateSourceObservation(equalSalary));
    }

    TEST(SourceObservationValidation, EnforcesObservationFieldBounds)
    {
        SourceObservation externalIdLimit = MakeObservation();
        externalIdLimit.jobs[0].externalId = std::string(255, 'x');
        EXPECT_TRUE(ValidateSourceObservation(externalIdLimit));

        SourceObservation externalIdOver = MakeObservation();
        externalIdOver.jobs[0].externalId = std::string(256, 'x');
        EXPECT_FALSE(ValidateSourceObservation(externalIdOver));

        SourceObservation publishedAt = MakeObservation();
        publishedAt.jobs[0].publishedAt = "2026-10-01 09:30:00";
        EXPECT_TRUE(ValidateSourceObservation(publishedAt));

        SourceObservation malformedPublishedAt = MakeObservation();
        malformedPublishedAt.jobs[0].publishedAt = "2026-10-01T09:30:00";
        EXPECT_FALSE(ValidateSourceObservation(malformedPublishedAt));
    }

    TEST(SourceObservationValidation, ValidatesThe0014ExtraFields)
    {
        SourceObservation valid = MakeObservation();
        valid.jobs[0].job.educationRequired = 3;
        valid.jobs[0].job.experienceYearsMin = 0;
        valid.jobs[0].job.experienceYearsMax = 3;
        valid.jobs[0].job.jobNature = 1;
        valid.jobs[0].job.deadline = "2026-10-31 04:00:00";
        valid.jobs[0].job.deadlineDateOnly = true;
        EXPECT_TRUE(ValidateSourceObservation(valid));

        // An explicit Beijing instant is preserved, never rewritten to noon.
        SourceObservation explicitTime = MakeObservation();
        explicitTime.jobs[0].job.deadline = "2026-10-31 10:30:00";
        explicitTime.jobs[0].job.deadlineDateOnly = false;
        EXPECT_TRUE(ValidateSourceObservation(explicitTime));

        SourceObservation educationOutOfRange = MakeObservation();
        educationOutOfRange.jobs[0].job.educationRequired = 6;
        EXPECT_FALSE(ValidateSourceObservation(educationOutOfRange));

        SourceObservation natureOutOfRange = MakeObservation();
        natureOutOfRange.jobs[0].job.jobNature = 4;
        EXPECT_FALSE(ValidateSourceObservation(natureOutOfRange));

        SourceObservation invertedYears = MakeObservation();
        invertedYears.jobs[0].job.experienceYearsMin = 5;
        invertedYears.jobs[0].job.experienceYearsMax = 2;
        EXPECT_FALSE(ValidateSourceObservation(invertedYears));

        SourceObservation yearsOverflow = MakeObservation();
        yearsOverflow.jobs[0].job.experienceYearsMax = 32768;
        EXPECT_FALSE(ValidateSourceObservation(yearsOverflow));

        SourceObservation malformedDeadline = MakeObservation();
        malformedDeadline.jobs[0].job.deadline = "2026-10-31T10:30:00";
        EXPECT_FALSE(ValidateSourceObservation(malformedDeadline));

        SourceObservation dateOnlyWrongInstant = MakeObservation();
        dateOnlyWrongInstant.jobs[0].job.deadline = "2026-10-31 10:30:00";
        dateOnlyWrongInstant.jobs[0].job.deadlineDateOnly = true;
        EXPECT_FALSE(ValidateSourceObservation(dateOnlyWrongInstant));

        SourceObservation flagWithoutDeadline = MakeObservation();
        flagWithoutDeadline.jobs[0].job.deadlineDateOnly = true;
        EXPECT_FALSE(ValidateSourceObservation(flagWithoutDeadline));
    }

    TEST(SourceObservationValidation, RejectsInvalidRequirements)
    {
        SourceObservation emptySkill = MakeObservation();
        emptySkill.jobs[0].requirements.push_back({"", 1});
        EXPECT_FALSE(ValidateSourceObservation(emptySkill));

        SourceObservation lowLevel = MakeObservation();
        lowLevel.jobs[0].requirements.push_back({"C++", 0});
        EXPECT_FALSE(ValidateSourceObservation(lowLevel));

        SourceObservation highLevel = MakeObservation();
        highLevel.jobs[0].requirements.push_back({"C++", 4});
        EXPECT_FALSE(ValidateSourceObservation(highLevel));

        SourceObservation duplicateSkill = MakeObservation();
        duplicateSkill.jobs[0].requirements.push_back({"C++", 1});
        duplicateSkill.jobs[0].requirements.push_back({"c++", 2});
        EXPECT_FALSE(ValidateSourceObservation(duplicateSkill));

        SourceObservation valid = MakeObservation();
        valid.jobs[0].requirements.push_back({"C++", 1});
        valid.jobs[0].requirements.push_back({"Linux", 2});
        EXPECT_TRUE(ValidateSourceObservation(valid));
    }

    TEST(SourceStatusStrings, ExposesContractSpelling)
    {
        EXPECT_STREQ(ToString(SourceStatus::kUnknown), "unknown");
        EXPECT_STREQ(ToString(SourceStatus::kOk), "ok");
        EXPECT_STREQ(ToString(SourceStatus::kMissing), "missing");
        EXPECT_STREQ(ToString(SourceStatus::kClosed), "closed");
    }

    TEST(SourceStatusStrings, RoundTripsKnownValues)
    {
        const SourceStatus values[] = {
            SourceStatus::kUnknown,
            SourceStatus::kOk,
            SourceStatus::kMissing,
            SourceStatus::kClosed};

        for (const SourceStatus status : values)
        {
            SourceStatus parsed = SourceStatus::kOk;

            ASSERT_TRUE(ParseSourceStatus(ToString(status), &parsed));
            EXPECT_EQ(parsed, status);
        }
    }

    TEST(SourceStatusStrings, RejectsUnknownSpelling)
    {
        SourceStatus parsed = SourceStatus::kOk;

        EXPECT_FALSE(ParseSourceStatus("active", &parsed));
        EXPECT_FALSE(ParseSourceStatus("", &parsed));
        EXPECT_FALSE(ParseSourceStatus("OK", &parsed));
        EXPECT_FALSE(ParseSourceStatus("ok", nullptr));
    }

    TEST(FreshnessStrings, ExposesContractSpelling)
    {
        EXPECT_STREQ(ToString(FreshnessState::kFresh), "fresh");
        EXPECT_STREQ(ToString(FreshnessState::kAging), "aging");
        EXPECT_STREQ(ToString(FreshnessState::kStale), "stale");
        EXPECT_STREQ(ToString(FreshnessState::kClosed), "closed");
    }

    TEST(FreshnessClassification, AppliesPolicyBoundaries)
    {
        EXPECT_EQ(
            ClassifyFreshness(false, 0),
            std::optional<FreshnessState>(FreshnessState::kFresh));

        EXPECT_EQ(
            ClassifyFreshness(false, kFreshMaxAgeSeconds),
            std::optional<FreshnessState>(FreshnessState::kFresh));

        EXPECT_EQ(
            ClassifyFreshness(false, kFreshMaxAgeSeconds + 1),
            std::optional<FreshnessState>(FreshnessState::kAging));

        EXPECT_EQ(
            ClassifyFreshness(false, kAgingMaxAgeSeconds),
            std::optional<FreshnessState>(FreshnessState::kAging));

        EXPECT_EQ(
            ClassifyFreshness(false, kAgingMaxAgeSeconds + 1),
            std::optional<FreshnessState>(FreshnessState::kStale));
    }

    TEST(FreshnessClassification, HandlesClosedAndUnobservedJobs)
    {
        EXPECT_EQ(
            ClassifyFreshness(true, std::nullopt),
            std::optional<FreshnessState>(FreshnessState::kClosed));

        EXPECT_EQ(
            ClassifyFreshness(true, 0),
            std::optional<FreshnessState>(FreshnessState::kClosed));

        EXPECT_EQ(
            ClassifyFreshness(false, std::nullopt),
            std::nullopt);

        EXPECT_EQ(
            ClassifyFreshness(false, -3600),
            std::optional<FreshnessState>(FreshnessState::kFresh));
    }

    TEST(MissingClosePolicy, AppliesCountAndGraceThresholds)
    {
        EXPECT_FALSE(ShouldCloseMissingJob(1, 0));
        EXPECT_TRUE(ShouldCloseMissingJob(kDefaultCloseAfterMisses, 0));

        EXPECT_FALSE(
            ShouldCloseMissingJob(1, kDefaultGraceSeconds));
        EXPECT_TRUE(
            ShouldCloseMissingJob(1, kDefaultGraceSeconds + 1));

        EXPECT_FALSE(
            ShouldCloseMissingJob(1, std::nullopt));
    }

    TEST(MissingClosePolicy, HonorsCustomPolicy)
    {
        const MissingPolicy custom{1, 3600};

        EXPECT_TRUE(ShouldCloseMissingJob(1, 0, custom));
        EXPECT_FALSE(ShouldCloseMissingJob(0, 0, custom));
        EXPECT_FALSE(ShouldCloseMissingJob(0, 3600, custom));
        EXPECT_TRUE(ShouldCloseMissingJob(0, 3601, custom));
    }

} // namespace jobpilot::service
