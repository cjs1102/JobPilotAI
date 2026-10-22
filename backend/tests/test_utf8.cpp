#include "jobpilot/core/utf8.h"

#include <string>

#include <gtest/gtest.h>

namespace jobpilot::core
{
    namespace
    {
        TEST(Utf8Test, AcceptsValidAsciiAndUnicode)
        {
            EXPECT_TRUE(IsValidUtf8(""));
            EXPECT_TRUE(IsValidUtf8("plain ASCII"));
            EXPECT_TRUE(IsValidUtf8("\xE4\xB8\xAD\xE6\x96\x87"));
            EXPECT_TRUE(IsValidUtf8("\xF0\x9F\x98\x80"));
            EXPECT_TRUE(IsValidUtf8("\xEF\xBF\xBF"));
            EXPECT_TRUE(IsValidUtf8("\xF4\x8F\xBF\xBF"));
        }

        TEST(Utf8Test, RejectsOverlongForms)
        {
            EXPECT_FALSE(IsValidUtf8(std::string("\xC0\xAF", 2)));
            EXPECT_FALSE(IsValidUtf8(std::string("\xE0\x80\xAF", 3)));
            EXPECT_FALSE(IsValidUtf8(std::string("\xF0\x80\x80\xAF", 4)));
        }

        TEST(Utf8Test, RejectsSurrogates)
        {
            EXPECT_FALSE(IsValidUtf8(std::string("\xED\xA0\x80", 3)));
            EXPECT_FALSE(IsValidUtf8(std::string("\xED\xBF\xBF", 3)));
        }

        TEST(Utf8Test, RejectsTruncatedSequences)
        {
            EXPECT_FALSE(IsValidUtf8(std::string("\xE2", 1)));
            EXPECT_FALSE(IsValidUtf8(std::string("\xE2\x82", 2)));
            EXPECT_FALSE(IsValidUtf8(std::string("\xF0\x9F\x98", 3)));
        }

        TEST(Utf8Test, RejectsStrayContinuationAndInvalidLeads)
        {
            EXPECT_FALSE(IsValidUtf8(std::string("\x80", 1)));
            EXPECT_FALSE(IsValidUtf8(std::string("\xBF", 1)));
            EXPECT_FALSE(IsValidUtf8(std::string("\xC0", 1)));
            EXPECT_FALSE(IsValidUtf8(std::string("\xFE", 1)));
            EXPECT_FALSE(IsValidUtf8(std::string("\xFF", 1)));
        }

        TEST(Utf8Test, RejectsValuesAboveUnicodeMaximum)
        {
            EXPECT_FALSE(IsValidUtf8(std::string("\xF4\x90\x80\x80", 4)));
        }
    } // namespace
} // namespace jobpilot::core
