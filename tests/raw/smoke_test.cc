#include <gtest/gtest.h>
#include <span>

#include "raw/parser.hpp"
#include "test_utils.hpp"

using namespace dex::raw;

// Parametrised smoke tests: parse_file and parse_buffer must succeed for every
// DEX file shipped in the test corpus.

class ParseAnyDexTest : public ::testing::TestWithParam<std::string>
{
};

TEST_P(ParseAnyDexTest, ParseFileSucceeds)
{
    auto result = parser::parse_file(GetParam());
    EXPECT_TRUE(result.has_value()) << result.error().message;
}

// ParseOptions.decode_strings = false skips the eager string-pool decode: the
// string_ids table is still populated (cheap, fixed-size), but strings() stays
// empty so the analysis layer can decode lazily.
TEST_P(ParseAnyDexTest, LazyStringsSkipsPoolDecode)
{
    auto buf = load_buf(GetParam());
    auto lazy = parser::parse_buffer(std::span(buf), {.decode_strings = false});
    ASSERT_TRUE(lazy.has_value()) << lazy.error().message;
    EXPECT_TRUE(lazy->strings().empty());
    EXPECT_FALSE(lazy->string_ids().empty());

    auto eager = parser::parse_buffer(std::span(buf));
    ASSERT_TRUE(eager.has_value());
    EXPECT_EQ(eager->strings().size(), lazy->string_ids().size());
}

TEST_P(ParseAnyDexTest, ParseBufferSucceeds)
{
    auto buf = load_buf(GetParam());
    auto result = parser::parse_buffer(std::span(buf));
    EXPECT_TRUE(result.has_value()) << result.error().message;
}

INSTANTIATE_TEST_SUITE_P(AllDexFiles, ParseAnyDexTest,
                         ::testing::Values("tests/data/classes.dex", "tests/data/multistring.dex",
                                           "tests/data/shapes.dex", "tests/data/trycatch.dex",
                                           "tests/data/polymorphic.dex"),
                         [](const testing::TestParamInfo<ParseAnyDexTest::ParamType> &info) {
                             std::string stem = info.param;
                             auto slash = stem.rfind('/');
                             if (slash != std::string::npos)
                                 stem = stem.substr(slash + 1);
                             auto dot = stem.rfind('.');
                             if (dot != std::string::npos)
                                 stem = stem.substr(0, dot);
                             return stem;
                         });
