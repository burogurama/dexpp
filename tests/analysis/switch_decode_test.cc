#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <vector>

#include "dex.hpp"

using namespace dex;

namespace {

// Helper: little-endian split of a signed 32-bit value across two code units.
inline void push_s32(std::vector<uint16_t> &v, int32_t x)
{
    auto u = static_cast<uint32_t>(x);
    v.push_back(static_cast<uint16_t>(u & 0xffff));
    v.push_back(static_cast<uint16_t>((u >> 16) & 0xffff));
}

// Helper: encode a packed-switch (0x2b) at the start of `out` with a given
// register and signed payload-offset (code units, relative to the switch).
inline void push_packed_switch(std::vector<uint16_t> &out, uint8_t reg, int32_t payload_off)
{
    out.push_back(static_cast<uint16_t>(0x2b | (reg << 8)));
    push_s32(out, payload_off);
}

inline void push_sparse_switch(std::vector<uint16_t> &out, uint8_t reg, int32_t payload_off)
{
    out.push_back(static_cast<uint16_t>(0x2c | (reg << 8)));
    push_s32(out, payload_off);
}

inline void push_return_void(std::vector<uint16_t> &out)
{
    out.push_back(0x000e); // op=0x0e (return-void), unused hi byte
}

} // namespace

TEST(SwitchDecodeTest, PackedKeysAndTargets)
{
    // packed-switch v0, +5 ; the payload sits 5 code units after the switch.
    // Layout:
    //   [0..2] packed-switch (3 units)
    //   [3]    return-void   (filler so payload doesn't immediately follow)
    //   [4]    return-void
    //   [5..]  packed-switch-payload
    std::vector<uint16_t> insns;
    push_packed_switch(insns, /*reg=*/0, /*payload_off=*/5);
    push_return_void(insns); // pos 3
    push_return_void(insns); // pos 4
    // payload at pos 5: ident=0x0100, size=3, first_key=10, targets={20, 30, 40}
    insns.push_back(0x0100);
    insns.push_back(3);
    push_s32(insns, 10);
    push_s32(insns, 20);
    push_s32(insns, 30);
    push_s32(insns, 40);

    auto out = decode_instructions(std::span<const uint16_t>(insns));
    ASSERT_FALSE(out.empty());
    auto *sw = std::get_if<SwitchInstruction>(&out[0]);
    ASSERT_NE(sw, nullptr);
    EXPECT_TRUE(sw->is_packed);
    EXPECT_EQ(sw->payload_offset, 5);
    ASSERT_EQ(sw->cases.size(), 3u);
    EXPECT_EQ(sw->cases[0].key, 10);
    EXPECT_EQ(sw->cases[0].target, 20);
    EXPECT_EQ(sw->cases[1].key, 11);
    EXPECT_EQ(sw->cases[1].target, 30);
    EXPECT_EQ(sw->cases[2].key, 12);
    EXPECT_EQ(sw->cases[2].target, 40);
}

TEST(SwitchDecodeTest, SparseKeysAndTargets)
{
    // sparse-switch v1, +5 with non-contiguous keys {-2, 0, 5}
    std::vector<uint16_t> insns;
    push_sparse_switch(insns, /*reg=*/1, /*payload_off=*/5);
    push_return_void(insns); // pos 3
    push_return_void(insns); // pos 4
    // payload at pos 5: ident=0x0200, size=3, keys[3], targets[3]
    insns.push_back(0x0200);
    insns.push_back(3);
    push_s32(insns, -2);
    push_s32(insns, 0);
    push_s32(insns, 5);
    push_s32(insns, 100);
    push_s32(insns, 200);
    push_s32(insns, 300);

    auto out = decode_instructions(std::span<const uint16_t>(insns));
    ASSERT_FALSE(out.empty());
    auto *sw = std::get_if<SwitchInstruction>(&out[0]);
    ASSERT_NE(sw, nullptr);
    EXPECT_FALSE(sw->is_packed);
    ASSERT_EQ(sw->cases.size(), 3u);
    EXPECT_EQ(sw->cases[0].key, -2);
    EXPECT_EQ(sw->cases[0].target, 100);
    EXPECT_EQ(sw->cases[1].key, 0);
    EXPECT_EQ(sw->cases[1].target, 200);
    EXPECT_EQ(sw->cases[2].key, 5);
    EXPECT_EQ(sw->cases[2].target, 300);
}

TEST(SwitchDecodeTest, NegativeTarget)
{
    // sparse-switch with a back-edge target (negative offset).
    std::vector<uint16_t> insns;
    push_sparse_switch(insns, /*reg=*/0, /*payload_off=*/3);
    // payload at pos 3: ident=0x0200, size=1, keys={7}, targets={-12}
    insns.push_back(0x0200);
    insns.push_back(1);
    push_s32(insns, 7);
    push_s32(insns, -12);

    auto out = decode_instructions(std::span<const uint16_t>(insns));
    ASSERT_FALSE(out.empty());
    auto *sw = std::get_if<SwitchInstruction>(&out[0]);
    ASSERT_NE(sw, nullptr);
    ASSERT_EQ(sw->cases.size(), 1u);
    EXPECT_EQ(sw->cases[0].key, 7);
    EXPECT_EQ(sw->cases[0].target, -12);
}

TEST(SwitchDecodeTest, MalformedPayloadEmptyCases)
{
    // packed-switch points at a payload whose ident byte is wrong.
    std::vector<uint16_t> insns;
    push_packed_switch(insns, /*reg=*/0, /*payload_off=*/3);
    // payload at pos 3 with WRONG sentinel (sparse 0x0200 instead of 0x0100)
    insns.push_back(0x0200);
    insns.push_back(2);
    push_s32(insns, 0);
    push_s32(insns, 1);
    push_s32(insns, 2);

    auto out = decode_instructions(std::span<const uint16_t>(insns));
    ASSERT_FALSE(out.empty());
    auto *sw = std::get_if<SwitchInstruction>(&out[0]);
    ASSERT_NE(sw, nullptr);
    EXPECT_TRUE(sw->is_packed);
    EXPECT_TRUE(sw->cases.empty()) << "Mismatched sentinel must yield empty cases";
}

// ===== type-instruction fixes (regression for the androguard differential) =====

// check-cast (0x1f, format 21c) is a single in-place register: src must be
// nullopt, not a duplicate of dest.
TEST(TypeInstructionDecode, CheckCastSingleRegister)
{
    // check-cast v3, type@7  ->  byte0 = (3 << 8) | 0x1f, then type index.
    std::vector<uint16_t> insns = {static_cast<uint16_t>((3 << 8) | 0x1f), 7};
    auto out = decode_instructions(std::span<const uint16_t>(insns));
    ASSERT_EQ(out.size(), 1u);
    const auto *t = std::get_if<TypeInstruction>(&out[0]);
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->dest, 3);
    EXPECT_EQ(t->type_index, 7u);
    EXPECT_FALSE(t->src.has_value());
}

// filled-new-array (0x24, format 35c) must capture every element register, not
// just the first — it is its own instruction with an arg-register list.
TEST(TypeInstructionDecode, FilledNewArrayCapturesAllRegisters)
{
    // filled-new-array {v1, v2, v3}, type@5
    //   byte0: A=3 (arg count) in high nibble, G=0, op=0x24  -> 0x3024
    //   unit1: type index = 5
    //   unit2: regs C,D,E packed 4 bits each -> (3<<8)|(2<<4)|1 = 0x321
    std::vector<uint16_t> insns = {0x3024, 5, 0x321};
    auto out = decode_instructions(std::span<const uint16_t>(insns));
    ASSERT_EQ(out.size(), 1u);
    const auto *f = std::get_if<FilledNewArrayInstruction>(&out[0]);
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->type_index, 5u);
    EXPECT_EQ(f->arg_regs, (std::vector<uint16_t>{1, 2, 3}));
}

// filled-new-array/range (0x25, format 3rc) over a contiguous register range.
TEST(TypeInstructionDecode, FilledNewArrayRangeCapturesAllRegisters)
{
    // filled-new-array/range {v4..v7}, type@9
    //   byte0: AA=4 (count) in high byte, op=0x25 -> 0x0425
    //   unit1: type index = 9
    //   unit2: CCCC = first register = 4
    std::vector<uint16_t> insns = {0x0425, 9, 4};
    auto out = decode_instructions(std::span<const uint16_t>(insns));
    ASSERT_EQ(out.size(), 1u);
    const auto *f = std::get_if<FilledNewArrayInstruction>(&out[0]);
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->type_index, 9u);
    EXPECT_EQ(f->arg_regs, (std::vector<uint16_t>{4, 5, 6, 7}));
}
