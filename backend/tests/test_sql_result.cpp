#include "jobpilot/database/sql_result.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace jobpilot::core
{

    TEST(SqlResultTest, StoresColumnsAndRows)
    {
        Result result(
            {"id", "email", "status"},
            {
                {"10001", "alice@example.com", "1"},
                {"10002", "bob@example.com", "0"},
            });

        EXPECT_EQ(result.rowCount(), 2U);
        EXPECT_EQ(result.columnCount(), 3U);

        EXPECT_EQ(result.columnNames()[0], "id");
        EXPECT_EQ(result.columnNames()[1], "email");
        EXPECT_EQ(result.columnNames()[2], "status");

        EXPECT_EQ(result.value(0, 0), "10001");
        EXPECT_EQ(result.value(0, 1), "alice@example.com");
        EXPECT_EQ(result.value(1, 2), "0");
    }

    TEST(SqlResultTest, PreservesNullAndEmptyString)
    {
        Result result(
            {"avatar", "remark"},
            {
                {std::nullopt, ""},
            });

        EXPECT_FALSE(result.value(0, 0).has_value());
        ASSERT_TRUE(result.value(0, 1).has_value());
        EXPECT_TRUE(result.value(0, 1)->empty());
    }

    TEST(SqlResultTest, RejectsMismatchedRow)
    {
        EXPECT_THROW(
            Result(
                {"id", "email"},
                {
                    {"10001"},
                }),
            std::invalid_argument);
    }

    TEST(SqlResultTest, ThrowsOnInvalidRowIndex)
    {
        Result result(
            {"id"},
            {
                {"10001"},
            });

        EXPECT_THROW(result.row(1), std::out_of_range);
    }

    TEST(SqlResultTest, ThrowsOnInvalidColumnIndex)
    {
        Result result(
            {"id"},
            {
                {"10001"},
            });

        EXPECT_THROW(result.value(0, 1), std::out_of_range);
    }

} // namespace jobpilot::core