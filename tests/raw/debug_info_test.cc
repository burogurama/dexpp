// raw::parser::parse_debug_info.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "raw/parser.hpp"
#include "test_utils.hpp"

using namespace dex::raw;

TEST(DebugInfo, DecodesEveryOpcode)
{
    // clang-format off
    const std::vector<uint8_t> item = {
        10,                                   // line_start
        2, 0, 5,                              // 2 parameter names: none, string 4
        DBG_ADVANCE_PC, 3,
        DBG_ADVANCE_LINE, 0x7e,               // -2
        DBG_START_LOCAL, 1, 4, 0,             // v1, name 3, no type
        DBG_START_LOCAL_EXTENDED, 2, 1, 2, 3, // v2, name 0, type 1, sig 2
        DBG_END_LOCAL, 1,
        DBG_RESTART_LOCAL, 1,
        DBG_SET_PROLOGUE_END,
        DBG_SET_EPILOGUE_BEGIN,
        DBG_SET_FILE, 0,                      // no file
        0x1f,                                 // a special opcode
        DBG_END_SEQUENCE,
    };
    // clang-format on
    auto info = parser::parse_debug_info(item, 0);
    ASSERT_TRUE(info.has_value()) << info.error().message;
    EXPECT_EQ(info->line_start, 10u);
    EXPECT_EQ(info->parameter_names, (std::vector<uint32_t>{NO_INDEX, 4}));
    ASSERT_EQ(info->ops.size(), 10u);
    EXPECT_EQ(info->ops[0].opcode, DBG_ADVANCE_PC);
    EXPECT_EQ(info->ops[0].addr_diff, 3u);
    EXPECT_EQ(info->ops[1].line_diff, -2);
    EXPECT_EQ(info->ops[2].reg, 1u);
    EXPECT_EQ(info->ops[2].name_idx, 3u);
    EXPECT_EQ(info->ops[2].type_idx, NO_INDEX);
    EXPECT_EQ(info->ops[3].opcode, DBG_START_LOCAL_EXTENDED);
    EXPECT_EQ(info->ops[3].name_idx, 0u);
    EXPECT_EQ(info->ops[3].type_idx, 1u);
    EXPECT_EQ(info->ops[3].sig_idx, 2u);
    EXPECT_EQ(info->ops[4].opcode, DBG_END_LOCAL);
    EXPECT_EQ(info->ops[5].opcode, DBG_RESTART_LOCAL);
    EXPECT_EQ(info->ops[8].opcode, DBG_SET_FILE);
    EXPECT_EQ(info->ops[8].name_idx, NO_INDEX);
    EXPECT_EQ(info->ops[9].opcode, 0x1f);
}

TEST(DebugInfo, RejectsMissingEnd)
{
    const std::vector<uint8_t> item = {1, 0, DBG_ADVANCE_PC, 2};
    EXPECT_FALSE(parser::parse_debug_info(item, 0).has_value());
}

TEST(DebugInfo, ParsesEveryFixtureMethod)
{
    std::size_t parsed = 0;
    for (const char *path : {"tests/data/classes.dex", "tests/data/fibonacci.dex",
                             "tests/data/androguard/classes.dex"}) {
        auto buf = load_buf(path);
        auto dex = parser::parse_buffer(buf);
        ASSERT_TRUE(dex.has_value());
        for (const ClassDefItem &cd : dex->class_defs()) {
            if (cd.class_data_off == 0)
                continue;
            auto data = parser::parse_class_data(buf, cd.class_data_off);
            ASSERT_TRUE(data.has_value());
            for (const auto *list : {&data->direct_methods, &data->virtual_methods})
                for (const EncodedMethod &m : *list) {
                    if (m.code_off == 0)
                        continue;
                    auto code = parser::parse_code_item(buf, m.code_off);
                    ASSERT_TRUE(code.has_value());
                    if (code->debug_info_off == 0)
                        continue;
                    auto info = parser::parse_debug_info(buf, code->debug_info_off);
                    ASSERT_TRUE(info.has_value()) << path;
                    ++parsed;
                }
        }
    }
    EXPECT_GT(parsed, 1000u);
}
