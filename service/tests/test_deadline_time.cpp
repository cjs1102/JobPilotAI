#include "jobpilot/service/job/job_extra_fields.h"

#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace jobpilot::service
{
    namespace
    {
        constexpr std::int64_t kEightHours = 8 * 60 * 60;

        void SetServerTimeZone(const char *zone)
        {
            const int result = ::setenv("TZ", zone, 1);
            ASSERT_EQ(result, 0);
            ::tzset();
        }

        JobImportData MakeJob()
        {
            JobImportData job;
            job.company = "Example";
            job.title = "Backend";
            job.source = "greenhouse";
            job.sourceUrl = "https://example.invalid/jobs/1";
            return job;
        }
    } // namespace

    TEST(DeadlineTime, ConvertsBetweenStoredUtcAndBeijing)
    {
        // Beijing noon is 04:00:00 UTC, so a calendar date round-trips.
        EXPECT_EQ(
            AddSecondsToDbTimestamp("2026-10-08 04:00:00", kEightHours),
            std::optional<std::string>("2026-10-08 12:00:00"));
        EXPECT_EQ(
            MakeDateOnlyStoredDeadline("2026-10-08"),
            std::optional<std::string>("2026-10-08 04:00:00"));
        EXPECT_EQ(
            DeadlineToBeijingLocal("2026-10-08 04:00:00"),
            std::optional<std::string>("2026-10-08 12:00:00"));
        EXPECT_EQ(
            DeadlineToBeijingDate("2026-10-08 04:00:00"),
            std::optional<std::string>("2026-10-08"));
        EXPECT_EQ(
            AddSecondsToDbTimestamp("2026-10-08 12:00:00", -kEightHours),
            std::optional<std::string>("2026-10-08 04:00:00"));
    }

    TEST(DeadlineTime, RoundTripsDateOnlyValuesWithoutShifting)
    {
        const std::vector<std::string> dates = {
            "2026-01-01",
            "2026-06-15",
            "2026-10-08",
            "2026-12-31",
            "2028-02-29",
            "1000-01-01",
            "9999-12-31"};

        for (const std::string &date : dates)
        {
            const std::optional<std::string> stored =
                MakeDateOnlyStoredDeadline(date);

            ASSERT_TRUE(stored.has_value()) << date;
            EXPECT_TRUE(IsDateOnlyStoredDeadline(stored.value()))
                << date;

            const std::optional<std::string> roundTripped =
                DeadlineToBeijingDate(stored.value());

            ASSERT_TRUE(roundTripped.has_value()) << date;
            EXPECT_EQ(roundTripped.value(), date) << date;
        }
    }

    TEST(DeadlineTime, AppliesDayMonthAndYearBoundaries)
    {
        // 16:00:00 UTC is the next Beijing calendar day.
        EXPECT_EQ(
            DeadlineToBeijingLocal("2026-10-07 16:00:00"),
            std::optional<std::string>("2026-10-08 00:00:00"));
        EXPECT_EQ(
            DeadlineToBeijingDate("2026-10-07 16:00:00"),
            std::optional<std::string>("2026-10-08"));

        // The last instant of a Beijing month/year belongs to the next day in UTC.
        EXPECT_EQ(
            MakeDateOnlyStoredDeadline("2026-12-31"),
            std::optional<std::string>("2026-12-31 04:00:00"));
        EXPECT_EQ(
            AddSecondsToDbTimestamp("2026-12-31 16:00:00", kEightHours),
            std::optional<std::string>("2027-01-01 00:00:00"));

        // Leap day in both directions.
        EXPECT_EQ(
            MakeDateOnlyStoredDeadline("2028-02-29"),
            std::optional<std::string>("2028-02-29 04:00:00"));
        EXPECT_EQ(
            DeadlineToBeijingDate("2028-02-29 20:00:00"),
            std::optional<std::string>("2028-03-01"));

        // Non-leap February and month-length edges are rejected, never rolled.
        EXPECT_FALSE(
            MakeDateOnlyStoredDeadline("2026-02-29").has_value());
        EXPECT_FALSE(
            MakeDateOnlyStoredDeadline("2026-04-31").has_value());
        EXPECT_FALSE(
            MakeDateOnlyStoredDeadline("2026-13-01").has_value());
        EXPECT_FALSE(
            MakeDateOnlyStoredDeadline("2026-00-10").has_value());
        EXPECT_FALSE(
            MakeDateOnlyStoredDeadline("2026-1-1").has_value());
        EXPECT_FALSE(
            MakeDateOnlyStoredDeadline("2026-10-08 12:00:00").has_value());
        EXPECT_FALSE(
            MakeDateOnlyStoredDeadline("").has_value());
    }

    TEST(DeadlineTime, RejectsUnrepresentableOrInvalidInputs)
    {
        EXPECT_FALSE(
            AddSecondsToDbTimestamp("not-a-timestamp", 0).has_value());
        EXPECT_FALSE(
            AddSecondsToDbTimestamp("2026-10-08T12:00:00", 0).has_value());
        EXPECT_FALSE(
            DeadlineToBeijingLocal("2026-10-08").has_value());
        // Beijing year 10000 is outside the representable range; the value is
        // refused instead of being silently labelled with a wrong time.
        EXPECT_FALSE(
            DeadlineToBeijingLocal("9999-12-31 20:00:00").has_value());
        EXPECT_FALSE(
            AddSecondsToDbTimestamp("1000-01-01 00:00:00", -1).has_value());
    }

    TEST(DeadlineTime, RecognisesOnlyTheNoonStorageConvention)
    {
        EXPECT_TRUE(
            IsDateOnlyStoredDeadline("2026-10-08 04:00:00"));
        EXPECT_FALSE(
            IsDateOnlyStoredDeadline("2026-10-08 03:59:59"));
        EXPECT_FALSE(
            IsDateOnlyStoredDeadline("2026-10-08 12:00:00"));
        EXPECT_FALSE(
            IsDateOnlyStoredDeadline("2026-10-08 04:00:01"));
        EXPECT_FALSE(IsDateOnlyStoredDeadline("2026-10-08"));
    }

    TEST(DeadlineTime, ConversionDoesNotUseTheServerTimeZone)
    {
        // The contract is fixed +08:00 (Asia/Shanghai); the host TZ must not
        // participate. These zones exist in every tzdata install, and the
        // +14 / -11 pair would shift a calendar day if localtime leaked in.
        const std::vector<const char *> zones = {
            "UTC",
            "Asia/Shanghai",
            "America/New_York",
            "Pacific/Kiritimati",
            "Pacific/Pago_Pago"};

        const std::string utc = "2026-10-07 16:00:00";

        for (const char *zone : zones)
        {
            SetServerTimeZone(zone);

            EXPECT_EQ(
                DeadlineToBeijingLocal(utc),
                std::optional<std::string>("2026-10-08 00:00:00"))
                << zone;
            EXPECT_EQ(
                DeadlineToBeijingDate(utc),
                std::optional<std::string>("2026-10-08"))
                << zone;
            EXPECT_EQ(
                MakeDateOnlyStoredDeadline("2026-10-08"),
                std::optional<std::string>("2026-10-08 04:00:00"))
                << zone;
        }

        SetServerTimeZone("UTC");
    }

    TEST(DeadlineValidation, AcceptsBothKindsAndPreservesExplicitTime)
    {
        JobImportData dateOnly = MakeJob();
        ASSERT_TRUE(
            MakeDateOnlyStoredDeadline("2026-10-31").has_value());
        dateOnly.deadline = "2026-10-31 04:00:00";
        dateOnly.deadlineDateOnly = true;
        EXPECT_TRUE(ValidateJobExtraFields(dateOnly));

        // An explicit instant is preserved verbatim; a real 18:30 Beijing
        // deadline is 10:30 UTC and must never be rewritten to noon.
        JobImportData explicitTime = MakeJob();
        explicitTime.deadline = "2026-10-31 10:30:00";
        explicitTime.deadlineDateOnly = false;
        EXPECT_TRUE(ValidateJobExtraFields(explicitTime));
        EXPECT_EQ(
            DeadlineToBeijingLocal(
                explicitTime.deadline.value()),
            std::optional<std::string>("2026-10-31 18:30:00"));

        // Unknown kind (legacy/administrative row) is valid and reports null.
        JobImportData unknownKind = MakeJob();
        unknownKind.deadline = "2026-10-31 10:30:00";
        EXPECT_TRUE(ValidateJobExtraFields(unknownKind));
    }

    TEST(DeadlineValidation, RejectsMismatchedOrMalformedDeadlines)
    {
        JobImportData dateOnlyWrongInstant = MakeJob();
        dateOnlyWrongInstant.deadline = "2026-10-31 10:30:00";
        dateOnlyWrongInstant.deadlineDateOnly = true;
        EXPECT_FALSE(
            ValidateJobExtraFields(dateOnlyWrongInstant));

        JobImportData flagWithoutDeadline = MakeJob();
        flagWithoutDeadline.deadlineDateOnly = false;
        EXPECT_FALSE(ValidateJobExtraFields(flagWithoutDeadline));

        for (const char *value :
             {"", "2026-10-31", "2026-10-31T10:30:00",
              "2026-13-01 10:30:00", "2026-02-29 10:30:00",
              "2026-10-31 24:00:00"})
        {
            JobImportData malformed = MakeJob();
            malformed.deadline = value;
            EXPECT_FALSE(ValidateJobExtraFields(malformed)) << value;
        }
    }

    TEST(DeadlineValidation, EnforcesEnumAndRangeDomains)
    {
        for (const std::uint32_t value : {1U, 2U, 3U, 4U, 5U})
        {
            JobImportData job = MakeJob();
            job.educationRequired = value;
            EXPECT_TRUE(ValidateJobExtraFields(job)) << value;
        }

        for (const std::uint32_t value : {0U, 6U, 255U})
        {
            JobImportData job = MakeJob();
            job.educationRequired = value;
            EXPECT_FALSE(ValidateJobExtraFields(job)) << value;
        }

        for (const std::uint32_t value : {1U, 2U, 3U})
        {
            JobImportData job = MakeJob();
            job.jobNature = value;
            EXPECT_TRUE(ValidateJobExtraFields(job)) << value;
        }

        for (const std::uint32_t value : {0U, 4U, 255U})
        {
            JobImportData job = MakeJob();
            job.jobNature = value;
            EXPECT_FALSE(ValidateJobExtraFields(job)) << value;
        }

        JobImportData maxYears = MakeJob();
        maxYears.experienceYearsMin = 0;
        maxYears.experienceYearsMax = 32767;
        EXPECT_TRUE(ValidateJobExtraFields(maxYears));

        JobImportData overYears = MakeJob();
        overYears.experienceYearsMin = 32768;
        EXPECT_FALSE(ValidateJobExtraFields(overYears));

        JobImportData inverted = MakeJob();
        inverted.experienceYearsMin = 5;
        inverted.experienceYearsMax = 3;
        EXPECT_FALSE(ValidateJobExtraFields(inverted));

        JobImportData equal = MakeJob();
        equal.experienceYearsMin = 3;
        equal.experienceYearsMax = 3;
        EXPECT_TRUE(ValidateJobExtraFields(equal));
    }

    TEST(DeadlineNormalization, DefaultsOnlyNextToAPresentDeadline)
    {
        JobImportData explicitDeadline = MakeJob();
        explicitDeadline.deadline = "2026-10-31 10:30:00";
        NormalizeJobExtraFields(&explicitDeadline);
        ASSERT_TRUE(
            explicitDeadline.deadlineDateOnly.has_value());
        EXPECT_FALSE(
            explicitDeadline.deadlineDateOnly.value());

        // A flag without a deadline must survive normalization so validation
        // rejects the inconsistent request instead of dropping the key.
        JobImportData noDeadline = MakeJob();
        noDeadline.deadlineDateOnly = true;
        NormalizeJobExtraFields(&noDeadline);
        ASSERT_TRUE(noDeadline.deadlineDateOnly.has_value());
        EXPECT_TRUE(noDeadline.deadlineDateOnly.value());
        EXPECT_FALSE(ValidateJobExtraFields(noDeadline));

        JobImportData dateOnly = MakeJob();
        dateOnly.deadline = "2026-10-31 04:00:00";
        dateOnly.deadlineDateOnly = true;
        NormalizeJobExtraFields(&dateOnly);
        ASSERT_TRUE(dateOnly.deadlineDateOnly.has_value());
        EXPECT_TRUE(dateOnly.deadlineDateOnly.value());

        NormalizeJobExtraFields(nullptr);
    }

} // namespace jobpilot::service
