// The opcode table and the format-level decoder / encoder (raw/opcodes.hpp).

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "raw/opcodes.hpp"

using namespace dex::raw;

TEST(Opcodes, TableIsConsistent)
{
    std::set<std::string_view> names;
    int used = 0;
    for (int op = 0; op < 256; ++op) {
        const OpcodeInfo &info = opcode_info(static_cast<uint8_t>(op));
        EXPECT_EQ(info.units, format_units(info.format)) << info.name;
        if (info.format == Format::Unused) {
            EXPECT_TRUE(info.name.starts_with("unused-")) << info.name;
            continue;
        }
        ++used;
        EXPECT_TRUE(names.insert(info.name).second) << "duplicate " << info.name;
    }
    EXPECT_EQ(used, 256 - 6 - 1 - 2 - 23); // 0x3e-0x43, 0x73, 0x79-0x7a, 0xe3-0xf9
    EXPECT_EQ(opcode_info(0x6e).name, "invoke-virtual");
    EXPECT_EQ(opcode_info(0x6e).ref, RefKind::Method);
    EXPECT_EQ(opcode_info(0xfa).format, Format::F45cc);
    EXPECT_EQ(opcode_info(0xff).ref, RefKind::Proto);
    EXPECT_EQ(opcode_info(0x18).units, 5); // const-wide
}

TEST(Opcodes, DecodeKnownEncodings)
{
    // invoke-virtual {v2, v1}, method@0x0017
    std::vector<uint16_t> code = {0x206e, 0x0017, 0x0012};
    auto f = decode_insn(code, 0);
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->opcode, 0x6e);
    EXPECT_EQ(f->a, 2u);
    EXPECT_EQ(f->index, 0x17u);
    EXPECT_EQ(f->args[0], 2);
    EXPECT_EQ(f->args[1], 1);

    // const/4 v1, #-1
    code = {0xf112};
    f = decode_insn(code, 0);
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->a, 1u);
    EXPECT_EQ(f->literal, -1);

    // goto/32 +0 (a branch to itself)
    code = {0x002a, 0x0000, 0x0000};
    f = decode_insn(code, 0);
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->offset, 0);

    // invoke-polymorphic {v1, v2}, meth@0005, proto@0009
    code = {0x20fa, 0x0005, 0x0021, 0x0009};
    f = decode_insn(code, 0);
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->index, 5u);
    EXPECT_EQ(f->proto, 9u);
}

TEST(Opcodes, DecodeRejectsBadInput)
{
    std::vector<uint16_t> unused = {0x003e};
    EXPECT_FALSE(decode_insn(unused, 0).has_value());
    std::vector<uint16_t> payload = {0x0100, 0x0000};
    EXPECT_FALSE(decode_insn(payload, 0).has_value());
    std::vector<uint16_t> cut = {0x0014, 0x0001}; // const needs 3 units
    EXPECT_FALSE(decode_insn(cut, 0).has_value());
    std::vector<uint16_t> six_args = {0x606e, 0x0000, 0x0000};
    EXPECT_FALSE(decode_insn(six_args, 0).has_value());
}

TEST(Opcodes, EncodeChecksFieldWidths)
{
    std::vector<uint16_t> out;
    InsnFields f;
    f.opcode = 0x01; // move vA, vB: 4-bit registers
    f.a = 15;
    f.b = 16;
    EXPECT_FALSE(encode_insn(f, out));
    EXPECT_TRUE(out.empty());
    f.b = 15;
    EXPECT_TRUE(encode_insn(f, out));
    EXPECT_EQ(out, (std::vector<uint16_t>{0xff01}));

    out.clear();
    f = {};
    f.opcode = 0x12; // const/4: literal -8..7
    f.literal = 8;
    EXPECT_FALSE(encode_insn(f, out));
    f.literal = -8;
    EXPECT_TRUE(encode_insn(f, out));

    out.clear();
    f = {};
    f.opcode = 0x28; // goto +AA
    f.offset = 128;
    EXPECT_FALSE(encode_insn(f, out));

    f = {};
    f.opcode = 0x3e; // unused
    EXPECT_FALSE(encode_insn(f, out));
}

TEST(Opcodes, EncodeDecodeRoundTripForEveryOpcode)
{
    // For every used opcode, random operand fields that fit the format encode
    // and decode back to the same fields.
    std::mt19937 rng(20261020);
    auto bits = [&](int n) { return static_cast<uint32_t>(rng() & ((1ull << n) - 1)); };
    auto signed_bits = [&](int n) {
        int64_t v = static_cast<int64_t>(rng()) | (static_cast<int64_t>(rng()) << 32);
        return n == 64 ? v : (v << (64 - n)) >> (64 - n);
    };
    for (int op = 0; op < 256; ++op) {
        const OpcodeInfo &info = opcode_info(static_cast<uint8_t>(op));
        if (info.format == Format::Unused)
            continue;
        for (int round = 0; round < 50; ++round) {
            InsnFields f;
            f.opcode = static_cast<uint8_t>(op);
            switch (info.format) {
            case Format::F10x:
                break;
            case Format::F12x:
                f.a = bits(4);
                f.b = bits(4);
                break;
            case Format::F11n:
                f.a = bits(4);
                f.literal = signed_bits(4);
                break;
            case Format::F11x:
                f.a = bits(8);
                break;
            case Format::F10t:
                f.offset = static_cast<int32_t>(signed_bits(8));
                break;
            case Format::F20t:
                f.offset = static_cast<int32_t>(signed_bits(16));
                break;
            case Format::F22x:
                f.a = bits(8);
                f.b = bits(16);
                break;
            case Format::F21t:
                f.a = bits(8);
                f.offset = static_cast<int32_t>(signed_bits(16));
                break;
            case Format::F21s:
            case Format::F21h:
                f.a = bits(8);
                f.literal = signed_bits(16);
                break;
            case Format::F21c:
                f.a = bits(8);
                f.index = bits(16);
                break;
            case Format::F23x:
                f.a = bits(8);
                f.b = bits(8);
                f.c = bits(8);
                break;
            case Format::F22b:
                f.a = bits(8);
                f.b = bits(8);
                f.literal = signed_bits(8);
                break;
            case Format::F22t:
                f.a = bits(4);
                f.b = bits(4);
                f.offset = static_cast<int32_t>(signed_bits(16));
                break;
            case Format::F22s:
                f.a = bits(4);
                f.b = bits(4);
                f.literal = signed_bits(16);
                break;
            case Format::F22c:
                f.a = bits(4);
                f.b = bits(4);
                f.index = bits(16);
                break;
            case Format::F32x:
                f.a = bits(16);
                f.b = bits(16);
                break;
            case Format::F30t:
            case Format::F31t:
                f.a = info.format == Format::F31t ? bits(8) : 0;
                f.offset = static_cast<int32_t>(signed_bits(32));
                break;
            case Format::F31i:
                f.a = bits(8);
                f.literal = signed_bits(32);
                break;
            case Format::F31c:
                f.a = bits(8);
                f.index = bits(32);
                break;
            case Format::F35c:
            case Format::F45cc:
                f.a = rng() % 6;
                for (uint32_t i = 0; i < f.a; ++i)
                    f.args[i] = static_cast<uint16_t>(bits(4));
                f.index = bits(16);
                if (info.format == Format::F45cc)
                    f.proto = bits(16);
                break;
            case Format::F3rc:
            case Format::F4rcc:
                f.a = bits(8);
                f.c = bits(16) % (0x10000 - f.a);
                f.index = bits(16);
                if (info.format == Format::F4rcc)
                    f.proto = bits(16);
                break;
            case Format::F51l:
                f.a = bits(8);
                f.literal = signed_bits(64);
                break;
            case Format::Unused:
                break;
            }
            std::vector<uint16_t> code;
            ASSERT_TRUE(encode_insn(f, code)) << info.name;
            ASSERT_EQ(code.size(), info.units) << info.name;
            auto back = decode_insn(code, 0);
            ASSERT_TRUE(back.has_value()) << info.name;
            EXPECT_EQ(*back, f) << info.name;
        }
    }
}

TEST(Opcodes, PayloadUnits)
{
    std::vector<uint16_t> packed = {0x0100, 3, 0, 0, 1, 0, 2, 0, 3, 0};
    EXPECT_EQ(payload_units(packed, 0), 4u + 2 * 3);
    std::vector<uint16_t> sparse = {0x0200, 2};
    EXPECT_EQ(payload_units(sparse, 0), 2u + 4 * 2); // declared size, even if cut off
    std::vector<uint16_t> fill = {0x0300, 4, 3, 0};
    EXPECT_EQ(payload_units(fill, 0), 4u + 6);
    std::vector<uint16_t> odd_bytes = {0x0300, 1, 3, 0};
    EXPECT_EQ(payload_units(odd_bytes, 0), 4u + 2);
    std::vector<uint16_t> huge = {0x0300, 0xFFFF, 0xFFFF, 0xFFFF};
    EXPECT_EQ(payload_units(huge, 0), 4 + (uint64_t{0xFFFF} * 0xFFFFFFFF + 1) / 2);
    std::vector<uint16_t> not_payload = {0x0001};
    EXPECT_FALSE(payload_units(not_payload, 0).has_value());
    std::vector<uint16_t> cut = {0x0300, 1};
    EXPECT_FALSE(payload_units(cut, 0).has_value());
}
