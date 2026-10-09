// Holistic raw-parser tests for tests/data/trycatch.dex.
//
// Ground truth from baksmali:
//   class_data_off=0x451
//     direct_method[0]: <init>()V          code_off=0x23c  tries_size=0
//     direct_method[1]: divide(II)I        code_off=0x1a8  tries_size=1
//     direct_method[2]: safeParse(String)String  code_off=0x1d0  tries_size=2

#include <gtest/gtest.h>
#include <span>

#include "raw/parser.hpp"
#include "raw/types.hpp"
#include "test_utils.hpp"

using namespace dex::raw;

TEST(TryCatchDex, OneClassDef)
{
    auto dex = parser::parse_file("tests/data/trycatch.dex");
    ASSERT_TRUE(dex.has_value()) << dex.error().message;
    EXPECT_EQ(dex->class_defs().size(), 1u);
}

TEST(TryCatchDex, ClassDataThreeDirectMethods)
{
    auto buf = load_buf("tests/data/trycatch.dex");
    auto cd = parser::parse_class_data(std::span(buf), 0x451);
    ASSERT_TRUE(cd.has_value()) << cd.error().message;
    EXPECT_EQ(cd->direct_methods.size(), 3u);
    EXPECT_EQ(cd->virtual_methods.size(), 0u);
}

TEST(TryCatchDex, DivideCodeItemOneTryBlock)
{
    auto buf = load_buf("tests/data/trycatch.dex");
    auto ci = parser::parse_code_item(std::span(buf), 0x1a8);
    ASSERT_TRUE(ci.has_value()) << ci.error().message;
    EXPECT_EQ(ci->tries_size, 1u);
    ASSERT_EQ(ci->tries.size(), 1u);
    EXPECT_EQ(ci->tries[0].start_addr, 0u);
    EXPECT_EQ(ci->tries[0].insn_count, 1u);
}

TEST(TryCatchDex, DivideCatchesArithmeticException)
{
    auto buf = load_buf("tests/data/trycatch.dex");
    auto ci = parser::parse_code_item(std::span(buf), 0x1a8);
    ASSERT_TRUE(ci.has_value()) << ci.error().message;
    ASSERT_FALSE(ci->handlers.empty());
    ASSERT_FALSE(ci->handlers[0].handlers.empty());
    EXPECT_EQ(ci->handlers[0].handlers[0].type_idx, 3u); // Ljava/lang/ArithmeticException;
}

TEST(TryCatchDex, SafeParseCodeItemTwoTryBlocks)
{
    auto buf = load_buf("tests/data/trycatch.dex");
    auto ci = parser::parse_code_item(std::span(buf), 0x1d0);
    ASSERT_TRUE(ci.has_value()) << ci.error().message;
    EXPECT_EQ(ci->tries_size, 2u);
    EXPECT_EQ(ci->tries.size(), 2u);
}

TEST(TryCatchDex, HandlerByteOffsetsMatch)
{
    auto buf = load_buf("tests/data/trycatch.dex");
    auto ci = parser::parse_code_item(std::span(buf), 0x1a8); // divide()
    ASSERT_TRUE(ci.has_value()) << ci.error().message;

    ASSERT_EQ(ci->handler_byte_offsets.size(), ci->handlers.size())
        << "handler_byte_offsets must be one-to-one with handlers";
    ASSERT_EQ(ci->tries.size(), 1u);

    bool found = false;
    for (uint16_t off : ci->handler_byte_offsets) {
        if (off == ci->tries[0].handler_off) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "tries[0].handler_off=0x" << std::hex << ci->tries[0].handler_off
                       << " must appear in handler_byte_offsets";
}
