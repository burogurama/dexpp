// Holistic raw-parser tests for tests/data/multistring.dex.
//
// Ground truth from baksmali:
//   29 strings including empty, embedded-null, and emoji entries.

#include <gtest/gtest.h>
#include <span>

#include "raw/parser.hpp"
#include "test_utils.hpp"

using namespace dex::raw;

TEST(MultiStringDex, TwentyNineStrings)
{
    auto buf = load_buf("tests/data/multistring.dex");
    auto header = parser::parse_header(std::span(buf));
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->string_ids_size, 29u);
}

TEST(MultiStringDex, ParsesAllStrings)
{
    auto buf = load_buf("tests/data/multistring.dex");
    auto header = parser::parse_header(std::span(buf));
    ASSERT_TRUE(header.has_value());

    auto ids_result =
        parser::parse_string_ids(std::span(buf), header->string_ids_off, header->string_ids_size);
    ASSERT_TRUE(ids_result.has_value());

    auto result = parser::parse_strings(std::span(buf), *ids_result);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    const auto &strings = result.value();
    ASSERT_EQ(strings.size(), 29u);

    EXPECT_EQ(strings[0], "");
    EXPECT_EQ(strings[1], "<clinit>");
    EXPECT_EQ(strings[2], "<init>");
    EXPECT_EQ(strings[3], "Goodbye, World!");
    EXPECT_EQ(strings[4], "Hello, World!");
    EXPECT_EQ(strings[5], "How are you?");
    EXPECT_EQ(strings[6], "I am fine, thanks!");
    EXPECT_EQ(strings[7], "LMultiStringTest;");
    EXPECT_EQ(strings[16], "answer");
    EXPECT_EQ(strings[17], "emoji");
    EXPECT_EQ(strings[18], "empty");
    EXPECT_EQ(strings[19], "farewell");
    EXPECT_EQ(strings[20], "greeting");
    EXPECT_EQ(strings[21], "main");
    EXPECT_EQ(strings[24], "question");
    EXPECT_EQ(strings[26], "withNull");
}

TEST(MultiStringDex, StringWithEmbeddedNull)
{
    auto buf = load_buf("tests/data/multistring.dex");
    auto header = parser::parse_header(std::span(buf));
    ASSERT_TRUE(header.has_value());

    auto ids_result =
        parser::parse_string_ids(std::span(buf), header->string_ids_off, header->string_ids_size);
    ASSERT_TRUE(ids_result.has_value());

    auto result = parser::parse_strings(std::span(buf), *ids_result);
    ASSERT_TRUE(result.has_value());
    const auto &strings = result.value();

    // strings[25] = "test\0embedded" (MUTF-8 encoded null)
    const std::string &s = strings[25];
    EXPECT_EQ(s.size(), 13u);
    EXPECT_EQ(s.substr(0, 4), "test");
    EXPECT_EQ(s[4], '\0');
    EXPECT_EQ(s.substr(5), "embedded");
}

TEST(MultiStringDex, EmojiStringIsNonEmpty)
{
    auto buf = load_buf("tests/data/multistring.dex");
    auto header = parser::parse_header(std::span(buf));
    ASSERT_TRUE(header.has_value());

    auto ids_result =
        parser::parse_string_ids(std::span(buf), header->string_ids_off, header->string_ids_size);
    ASSERT_TRUE(ids_result.has_value());

    auto result = parser::parse_strings(std::span(buf), *ids_result);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result.value()[28].empty());
}
