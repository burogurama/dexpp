#include "instruction.hpp"

#include <cassert>
#include <cstdint>
#include <format>
#include <type_traits>

namespace dex {

// clang-format off
static constexpr const char *kOpName[256] = {
    // 0x00-0x0f
    "nop", "move", "move/from16", "move/16",
    "move-wide", "move-wide/from16", "move-wide/16", "move-object",
    "move-object/from16", "move-object/16", "move-result", "move-result-wide",
    "move-result-object", "move-exception", "return-void", "return",
    // 0x10-0x1f
    "return-wide", "return-object", "const/4", "const/16",
    "const", "const/high16", "const-wide/16", "const-wide/32",
    "const-wide", "const-wide/high16", "const-string", "const-string/jumbo",
    "const-class", "monitor-enter", "monitor-exit", "check-cast",
    // 0x20-0x2f
    "instance-of", "array-length", "new-instance", "new-array",
    "filled-new-array", "filled-new-array/range", "fill-array-data", "throw",
    "goto", "goto/16", "goto/32", "packed-switch",
    "sparse-switch", "cmpl-float", "cmpg-float", "cmpl-double",
    // 0x30-0x3f
    "cmpg-double", "cmp-long", "if-eq", "if-ne",
    "if-lt", "if-ge", "if-gt", "if-le",
    "if-eqz", "if-nez", "if-ltz", "if-gez",
    "if-gtz", "if-lez", "unused-3e", "unused-3f",
    // 0x40-0x4f
    "unused-40", "unused-41", "unused-42", "unused-43",
    "aget", "aget-wide", "aget-object", "aget-boolean",
    "aget-byte", "aget-char", "aget-short", "aput",
    "aput-wide", "aput-object", "aput-boolean", "aput-byte",
    // 0x50-0x5f
    "aput-char", "aput-short", "iget", "iget-wide",
    "iget-object", "iget-boolean", "iget-byte", "iget-char",
    "iget-short", "iput", "iput-wide", "iput-object",
    "iput-boolean", "iput-byte", "iput-char", "iput-short",
    // 0x60-0x6f
    "sget", "sget-wide", "sget-object", "sget-boolean",
    "sget-byte", "sget-char", "sget-short", "sput",
    "sput-wide", "sput-object", "sput-boolean", "sput-byte",
    "sput-char", "sput-short", "invoke-virtual", "invoke-super",
    // 0x70-0x7f
    "invoke-direct", "invoke-static", "invoke-interface", "unused-73",
    "invoke-virtual/range", "invoke-super/range", "invoke-direct/range", "invoke-static/range",
    "invoke-interface/range", "unused-79", "unused-7a", "neg-int",
    "not-int", "neg-long", "not-long", "neg-float",
    // 0x80-0x8f
    "neg-double", "int-to-long", "int-to-float", "int-to-double",
    "long-to-int", "long-to-float", "long-to-double", "float-to-int",
    "float-to-long", "float-to-double", "double-to-int", "double-to-long",
    "double-to-float", "int-to-byte", "int-to-char", "int-to-short",
    // 0x90-0x9f
    "add-int", "sub-int", "mul-int", "div-int",
    "rem-int", "and-int", "or-int", "xor-int",
    "shl-int", "shr-int", "ushr-int", "add-long",
    "sub-long", "mul-long", "div-long", "rem-long",
    // 0xa0-0xaf
    "and-long", "or-long", "xor-long", "shl-long",
    "shr-long", "ushr-long", "add-float", "sub-float",
    "mul-float", "div-float", "rem-float", "add-double",
    "sub-double", "mul-double", "div-double", "rem-double",
    // 0xb0-0xbf
    "add-int/2addr", "sub-int/2addr", "mul-int/2addr", "div-int/2addr",
    "rem-int/2addr", "and-int/2addr", "or-int/2addr", "xor-int/2addr",
    "shl-int/2addr", "shr-int/2addr", "ushr-int/2addr", "add-long/2addr",
    "sub-long/2addr", "mul-long/2addr", "div-long/2addr", "rem-long/2addr",
    // 0xc0-0xcf
    "and-long/2addr", "or-long/2addr", "xor-long/2addr", "shl-long/2addr",
    "shr-long/2addr", "ushr-long/2addr", "add-float/2addr", "sub-float/2addr",
    "mul-float/2addr", "div-float/2addr", "rem-float/2addr", "add-double/2addr",
    "sub-double/2addr", "mul-double/2addr", "div-double/2addr", "rem-double/2addr",
    // 0xd0-0xdf
    "add-int/lit16", "rsub-int", "mul-int/lit16", "div-int/lit16",
    "rem-int/lit16", "and-int/lit16", "or-int/lit16", "xor-int/lit16",
    "add-int/lit8", "rsub-int/lit8", "mul-int/lit8", "div-int/lit8",
    "rem-int/lit8", "and-int/lit8", "or-int/lit8", "xor-int/lit8",
    // 0xe0-0xef
    "shl-int/lit8", "shr-int/lit8", "ushr-int/lit8", "unused-e3",
    "unused-e4", "unused-e5", "unused-e6", "unused-e7",
    "unused-e8", "unused-e9", "unused-ea", "unused-eb",
    "unused-ec", "unused-ed", "unused-ee", "unused-ef",
    // 0xf0-0xff
    "unused-f0", "unused-f1", "unused-f2", "unused-f3",
    "unused-f4", "unused-f5", "unused-f6", "unused-f7",
    "unused-f8", "unused-f9", "invoke-polymorphic", "invoke-polymorphic/range",
    "invoke-custom", "invoke-custom/range", "const-method-handle", "const-method-type",
};
// clang-format on

std::string_view opcode_name(uint8_t opcode) { return kOpName[opcode]; }

namespace {

std::string join_regs(std::span<const uint16_t> regs)
{
    std::string out = "{";
    for (std::size_t j = 0; j < regs.size(); ++j) {
        if (j)
            out += ", ";
        out += std::format("v{}", regs[j]);
    }
    out += "}";
    return out;
}

std::string operands_of(const Instruction &insn)
{
    return std::visit(
        [](const auto &i) -> std::string {
            using T = std::decay_t<decltype(i)>;
            if constexpr (std::is_same_v<T, NopInstruction>) {
                return "";
            }
            else if constexpr (std::is_same_v<T, GotoInstruction>) {
                return std::format("{:+d}", i.branch_offset);
            }
            else if constexpr (std::is_same_v<T, IfInstruction>) {
                return std::format("v{}, v{}, {:+d}", i.reg_a, i.reg_b, i.branch_offset);
            }
            else if constexpr (std::is_same_v<T, IfZeroInstruction>) {
                return std::format("v{}, {:+d}", i.reg, i.branch_offset);
            }
            else if constexpr (std::is_same_v<T, SwitchInstruction>) {
                return std::format("v{}, {:+d}", i.reg, i.payload_offset);
            }
            else if constexpr (std::is_same_v<T, ReturnInstruction>) {
                return i.reg ? std::format("v{}", *i.reg) : std::string{};
            }
            else if constexpr (std::is_same_v<T, ThrowInstruction>) {
                return std::format("v{}", i.reg);
            }
            else if constexpr (std::is_same_v<T, MoveInstruction>) {
                // move-result* / move-exception (0x0a-0x0d) have no explicit source.
                if (i.base.opcode >= 0x0a && i.base.opcode <= 0x0d)
                    return std::format("v{}", i.dest);
                return std::format("v{}, v{}", i.dest, i.src);
            }
            else if constexpr (std::is_same_v<T, ConstInstruction>) {
                return std::format("v{}, #{}", i.dest, i.value);
            }
            else if constexpr (std::is_same_v<T, ConstStringInstruction>) {
                return std::format("v{}, string@{}", i.dest, i.string_index);
            }
            else if constexpr (std::is_same_v<T, ConstClassInstruction>) {
                return std::format("v{}, type@{}", i.dest, i.type_index);
            }
            else if constexpr (std::is_same_v<T, ConstHandleInstruction>) {
                return std::format("v{}, idx@{}", i.dest, i.index);
            }
            else if constexpr (std::is_same_v<T, TypeInstruction>) {
                std::string out = std::format("v{}, type@{}", i.dest, i.type_index);
                if (i.src)
                    out += std::format(", v{}", *i.src);
                return out;
            }
            else if constexpr (std::is_same_v<T, FilledNewArrayInstruction>) {
                return std::format("{} type@{}", join_regs(i.arg_regs), i.type_index);
            }
            else if constexpr (std::is_same_v<T, MonitorInstruction>) {
                return std::format("v{}", i.reg);
            }
            else if constexpr (std::is_same_v<T, FillArrayInstruction>) {
                return std::format("v{}, {:+d}", i.reg, i.payload_offset);
            }
            else if constexpr (std::is_same_v<T, CompareInstruction>) {
                return std::format("v{}, v{}, v{}", i.dest, i.src_a, i.src_b);
            }
            else if constexpr (std::is_same_v<T, ArrayOpInstruction>) {
                return std::format("v{}, v{}[v{}]", i.value_reg, i.array_reg, i.index_reg);
            }
            else if constexpr (std::is_same_v<T, FieldInstruction>) {
                if (i.object_reg)
                    return std::format("v{}, v{}.field@{}", i.value_reg, *i.object_reg,
                                       i.field_index);
                return std::format("v{}, field@{}", i.value_reg, i.field_index);
            }
            else if constexpr (std::is_same_v<T, InvokeInstruction>) {
                return std::format("method@{} {}", i.method_index, join_regs(i.arg_regs));
            }
            else if constexpr (std::is_same_v<T, InvokeCustomInstruction>) {
                return std::format("site@{} {}", i.call_site_index, join_regs(i.arg_regs));
            }
            else if constexpr (std::is_same_v<T, UnaryOpInstruction>) {
                return std::format("v{}, v{}", i.dest, i.src);
            }
            else if constexpr (std::is_same_v<T, BinaryOpInstruction>) {
                return std::format("v{}, v{}, v{}", i.dest, i.src_a, i.src_b);
            }
            else if constexpr (std::is_same_v<T, BinaryLitInstruction>) {
                return std::format("v{}, v{}, #{}", i.dest, i.src, i.literal);
            }
            return "";
        },
        insn);
}

} // namespace

std::string to_string(const Instruction &insn)
{
    std::string ops = operands_of(insn);
    std::string_view name = opcode_name(base_of(insn).opcode);
    if (ops.empty())
        return std::string(name);
    return std::format("{} {}", name, ops);
}

// Unused/reserved opcodes are given width 1 so the decoder can skip past them.
// clang-format off
static constexpr uint8_t kWidth[256] = {
    // 0x00  0x01  0x02  0x03  0x04  0x05  0x06  0x07  0x08  0x09  0x0a  0x0b  0x0c  0x0d  0x0e  0x0f
       1,    1,    2,    3,    1,    2,    3,    1,    2,    3,    1,    1,    1,    1,    1,    1,
    // 0x10  0x11  0x12  0x13  0x14  0x15  0x16  0x17  0x18  0x19  0x1a  0x1b  0x1c  0x1d  0x1e  0x1f
       1,    1,    1,    2,    3,    2,    2,    3,    5,    2,    2,    3,    2,    1,    1,    2,
    // 0x20  0x21  0x22  0x23  0x24  0x25  0x26  0x27  0x28  0x29  0x2a  0x2b  0x2c  0x2d  0x2e  0x2f
       2,    1,    2,    2,    3,    3,    3,    1,    1,    2,    3,    3,    3,    2,    2,    2,
    // 0x30  0x31  0x32  0x33  0x34  0x35  0x36  0x37  0x38  0x39  0x3a  0x3b  0x3c  0x3d  0x3e  0x3f
       2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    1,    1,
    // 0x40  0x41  0x42  0x43  0x44  0x45  0x46  0x47  0x48  0x49  0x4a  0x4b  0x4c  0x4d  0x4e  0x4f
       1,    1,    1,    1,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,
    // 0x50  0x51  0x52  0x53  0x54  0x55  0x56  0x57  0x58  0x59  0x5a  0x5b  0x5c  0x5d  0x5e  0x5f
       2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,
    // 0x60  0x61  0x62  0x63  0x64  0x65  0x66  0x67  0x68  0x69  0x6a  0x6b  0x6c  0x6d  0x6e  0x6f
       2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    3,    3,
    // 0x70  0x71  0x72  0x73  0x74  0x75  0x76  0x77  0x78  0x79  0x7a  0x7b  0x7c  0x7d  0x7e  0x7f
       3,    3,    3,    1,    3,    3,    3,    3,    3,    1,    1,    1,    1,    1,    1,    1,
    // 0x80  0x81  0x82  0x83  0x84  0x85  0x86  0x87  0x88  0x89  0x8a  0x8b  0x8c  0x8d  0x8e  0x8f
       1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,
    // 0x90  0x91  0x92  0x93  0x94  0x95  0x96  0x97  0x98  0x99  0x9a  0x9b  0x9c  0x9d  0x9e  0x9f
       2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,
    // 0xa0  0xa1  0xa2  0xa3  0xa4  0xa5  0xa6  0xa7  0xa8  0xa9  0xaa  0xab  0xac  0xad  0xae  0xaf
       2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,
    // 0xb0  0xb1  0xb2  0xb3  0xb4  0xb5  0xb6  0xb7  0xb8  0xb9  0xba  0xbb  0xbc  0xbd  0xbe  0xbf
       1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,
    // 0xc0  0xc1  0xc2  0xc3  0xc4  0xc5  0xc6  0xc7  0xc8  0xc9  0xca  0xcb  0xcc  0xcd  0xce  0xcf
       1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,
    // 0xd0  0xd1  0xd2  0xd3  0xd4  0xd5  0xd6  0xd7  0xd8  0xd9  0xda  0xdb  0xdc  0xdd  0xde  0xdf
       2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,    2,
    // 0xe0  0xe1  0xe2  0xe3..0xf9 (unused → 1)
       2,    2,    2,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,
    // 0xf0  0xf1  0xf2  0xf3  0xf4  0xf5  0xf6  0xf7  0xf8  0xf9  0xfa  0xfb  0xfc  0xfd  0xfe  0xff
       1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    4,    4,    3,    3,    2,    2,
};
// clang-format on

// Returns the OperandWidth for a 23x / 12x binary opcode (0x90–0xcf).
static OperandWidth binary_op_width(uint8_t op)
{
    if (op >= 0x90 && op <= 0xaf) {
        if (op >= 0x9b && op <= 0xa5)
            return OperandWidth::Pair;
        if (op >= 0xab && op <= 0xaf)
            return OperandWidth::Pair;
        return OperandWidth::Single;
    }
    if (op >= 0xb0 && op <= 0xcf) {
        uint8_t off = static_cast<uint8_t>(op - 0xb0);
        if (off >= 0x0b && off <= 0x15)
            return OperandWidth::Pair;
        if (off >= 0x1b && off <= 0x1f)
            return OperandWidth::Pair;
        return OperandWidth::Single;
    }
    return OperandWidth::Single;
}

// Decode format 35c argument registers.
// count = (unit0 >> 12) & 0xf, G = (unit0 >> 8) & 0xf, C/D/E/F packed in unit2.
static std::vector<uint16_t> decode_35c_regs(uint16_t unit0, uint16_t unit2)
{
    uint8_t count = static_cast<uint8_t>((unit0 >> 12) & 0xf);
    uint8_t reg_g = static_cast<uint8_t>((unit0 >> 8) & 0xf);

    std::vector<uint16_t> regs;
    regs.reserve(count);

    const uint8_t packed[5] = {
        static_cast<uint8_t>(unit2 & 0xf),
        static_cast<uint8_t>((unit2 >> 4) & 0xf),
        static_cast<uint8_t>((unit2 >> 8) & 0xf),
        static_cast<uint8_t>((unit2 >> 12) & 0xf),
        reg_g,
    };
    for (uint8_t i = 0; i < count && i < 5; ++i)
        regs.push_back(packed[i]);

    return regs;
}

// Decode format 3rc argument registers. count = AA (unit0 >> 8), first_reg = unit2.
static std::vector<uint16_t> decode_3rc_regs(uint16_t unit0, uint16_t unit2)
{
    uint8_t count = static_cast<uint8_t>(unit0 >> 8);
    uint16_t first_reg = unit2;

    std::vector<uint16_t> regs;
    regs.reserve(count);
    for (uint8_t i = 0; i < count; ++i)
        regs.push_back(static_cast<uint16_t>(first_reg + i));

    return regs;
}

// Returns the size in 16-bit code units of a payload pseudo-instruction.
// Precondition: insns[pos] has high byte != 0 and low byte == 0.
static uint32_t payload_size(std::span<const uint16_t> insns, uint32_t pos)
{
    if (pos >= insns.size())
        return 1;
    uint8_t kind = static_cast<uint8_t>(insns[pos] >> 8);
    switch (kind) {
    case 0x01: { // packed-switch-payload
        if (pos + 1 >= insns.size())
            return 1;
        uint16_t size = insns[pos + 1];
        return 4u + static_cast<uint32_t>(size) * 2u;
    }
    case 0x02: { // sparse-switch-payload
        if (pos + 1 >= insns.size())
            return 1;
        uint16_t size = insns[pos + 1];
        return 2u + static_cast<uint32_t>(size) * 4u;
    }
    case 0x03: { // fill-array-data-payload
        if (pos + 3 >= insns.size())
            return 1;
        uint16_t elem_width = insns[pos + 1];
        uint32_t elem_count =
            static_cast<uint32_t>(insns[pos + 2]) | (static_cast<uint32_t>(insns[pos + 3]) << 16);
        uint32_t data_units = (static_cast<uint32_t>(elem_width) * elem_count + 1u) / 2u;
        return 4u + data_units;
    }
    default:
        return 1;
    }
}

static int32_t sex8(uint8_t v) { return static_cast<int32_t>(static_cast<int8_t>(v)); }
static int32_t sex16(uint16_t v) { return static_cast<int32_t>(static_cast<int16_t>(v)); }
static int32_t sex4(uint8_t v) { return (v & 0x8) ? static_cast<int32_t>(v | 0xfffffff0u) : v; }

// Read a signed 32-bit value from two consecutive code units (little-endian within each).
static int32_t read_s32(std::span<const uint16_t> insns, uint32_t pos)
{
    uint32_t lo = insns[pos];
    uint32_t hi = insns[pos + 1];
    return static_cast<int32_t>(lo | (hi << 16));
}

// Decode the packed/sparse switch payload pointed to by `payload_offset` (in
// code units, relative to the switch instruction at `insn_off`).  On any
// bounds-check failure or sentinel mismatch, leaves `sw.cases` empty.
static void decode_switch_payload(std::span<const uint16_t> insns, uint32_t insn_off,
                                  int32_t payload_offset, SwitchInstruction &sw)
{
    int64_t pay64 = static_cast<int64_t>(insn_off) + static_cast<int64_t>(payload_offset);
    if (pay64 < 0 || pay64 + 2 > static_cast<int64_t>(insns.size()))
        return;
    uint32_t p = static_cast<uint32_t>(pay64);
    uint16_t ident = insns[p];

    if (sw.is_packed) {
        if (ident != 0x0100)
            return;
        if (static_cast<uint64_t>(p) + 4 > insns.size())
            return;
        uint16_t size = insns[p + 1];
        int32_t first_key = read_s32(insns, p + 2);
        if (static_cast<uint64_t>(p) + 4 + static_cast<uint64_t>(size) * 2 > insns.size())
            return;
        sw.cases.reserve(size);
        for (uint32_t i = 0; i < size; ++i) {
            int32_t target = read_s32(insns, p + 4 + 2 * i);
            sw.cases.push_back({first_key + static_cast<int32_t>(i), target});
        }
    }
    else {
        if (ident != 0x0200)
            return;
        if (static_cast<uint64_t>(p) + 2 > insns.size())
            return;
        uint16_t size = insns[p + 1];
        if (static_cast<uint64_t>(p) + 2 + static_cast<uint64_t>(size) * 4 > insns.size())
            return;
        sw.cases.resize(size);
        for (uint32_t i = 0; i < size; ++i)
            sw.cases[i].key = read_s32(insns, p + 2 + 2 * i);
        for (uint32_t i = 0; i < size; ++i)
            sw.cases[i].target = read_s32(insns, p + 2 + 2 * size + 2 * i);
    }
}

bool is_terminator(const Instruction &insn)
{
    return std::visit(
        [](const auto &i) -> bool {
            using T = std::decay_t<decltype(i)>;
            if constexpr (std::is_same_v<T, GotoInstruction> || std::is_same_v<T, IfInstruction> ||
                          std::is_same_v<T, IfZeroInstruction> ||
                          std::is_same_v<T, SwitchInstruction> ||
                          std::is_same_v<T, ReturnInstruction> ||
                          std::is_same_v<T, ThrowInstruction>) {
                return true;
            }
            return false;
        },
        insn);
}

std::vector<Instruction> decode_instructions(std::span<const uint16_t> insns)
{
    std::vector<Instruction> result;
    uint32_t pos = 0;

    while (pos < static_cast<uint32_t>(insns.size())) {
        uint16_t unit0 = insns[pos];
        uint8_t op = static_cast<uint8_t>(unit0 & 0xff);
        uint8_t hi = static_cast<uint8_t>(unit0 >> 8);

        // Payload pseudo-instructions: nop (op==0) with non-zero high byte
        if (op == 0x00 && hi != 0) {
            pos += payload_size(insns, pos);
            continue;
        }

        uint8_t sz = kWidth[op];

        // Safe read of additional code units
        uint16_t unit1 = (sz > 1 && pos + 1 < insns.size()) ? insns[pos + 1] : 0;
        uint16_t unit2 = (sz > 2 && pos + 2 < insns.size()) ? insns[pos + 2] : 0;
        uint16_t unit3 = (sz > 3 && pos + 3 < insns.size()) ? insns[pos + 3] : 0;
        uint16_t unit4 = (sz > 4 && pos + 4 < insns.size()) ? insns[pos + 4] : 0;

        InstructionBase base{pos, op, sz};

        if (op == 0x00) {
            result.push_back(NopInstruction{base});
        }
        else if (op == 0x01 || op == 0x04 || op == 0x07) {
            // format 12x: B|A op → dest=A, src=B
            uint8_t dest = hi & 0x0f;
            uint8_t src = (hi >> 4) & 0x0f;
            OperandWidth width = (op == 0x04) ? OperandWidth::Pair : OperandWidth::Single;
            result.push_back(MoveInstruction{base, dest, src, width});
        }
        else if (op == 0x02 || op == 0x05 || op == 0x08) {
            // format 22x: AA op BBBB → dest=AA, src=BBBB
            OperandWidth width = (op == 0x05) ? OperandWidth::Pair : OperandWidth::Single;
            result.push_back(MoveInstruction{base, hi, unit1, width});
        }
        else if (op == 0x03 || op == 0x06 || op == 0x09) {
            // format 32x: 00 op AAAA BBBB → dest=unit1, src=unit2
            OperandWidth width = (op == 0x06) ? OperandWidth::Pair : OperandWidth::Single;
            result.push_back(MoveInstruction{base, unit1, unit2, width});
        }
        else if (op >= 0x0a && op <= 0x0d) {
            // move-result / move-result-wide / move-result-object / move-exception
            // format 11x: AA op → dest=AA, src=0 (implicit)
            OperandWidth width = (op == 0x0b) ? OperandWidth::Pair : OperandWidth::Single;
            result.push_back(MoveInstruction{base, hi, 0, width});
        }
        else if (op == 0x0e) {
            result.push_back(ReturnInstruction{base, std::nullopt, OperandWidth::Single});
        }
        else if (op >= 0x0f && op <= 0x11) {
            // format 11x: AA op → reg=AA
            OperandWidth width = (op == 0x10) ? OperandWidth::Pair : OperandWidth::Single;
            result.push_back(ReturnInstruction{base, hi, width});
        }
        else if (op == 0x12) {
            // const/4 format 11n: B|A op → dest=A, val=sign_extend(B,4)
            result.push_back(ConstInstruction{base, static_cast<uint8_t>(hi & 0x0f),
                                              sex4(static_cast<uint8_t>((hi >> 4) & 0x0f)),
                                              OperandWidth::Single});
        }
        else if (op == 0x13) {
            // const/16 format 21s
            result.push_back(ConstInstruction{base, hi, sex16(unit1), OperandWidth::Single});
        }
        else if (op == 0x14) {
            // const format 31i
            int32_t val = static_cast<int32_t>(static_cast<uint32_t>(unit1) |
                                               (static_cast<uint32_t>(unit2) << 16));
            result.push_back(ConstInstruction{base, hi, val, OperandWidth::Single});
        }
        else if (op == 0x15) {
            // const/high16 format 21h
            int64_t val =
                static_cast<int64_t>(static_cast<int32_t>(static_cast<uint32_t>(unit1) << 16));
            result.push_back(ConstInstruction{base, hi, val, OperandWidth::Single});
        }
        else if (op == 0x16) {
            result.push_back(ConstInstruction{base, hi, sex16(unit1), OperandWidth::Pair});
        }
        else if (op == 0x17) {
            int32_t lo = static_cast<int32_t>(static_cast<uint32_t>(unit1) |
                                              (static_cast<uint32_t>(unit2) << 16));
            result.push_back(ConstInstruction{base, hi, lo, OperandWidth::Pair});
        }
        else if (op == 0x18) {
            // const-wide format 51l
            uint64_t lo = static_cast<uint32_t>(unit1) | (static_cast<uint32_t>(unit2) << 16);
            uint64_t hi64 = static_cast<uint32_t>(unit3) | (static_cast<uint32_t>(unit4) << 16);
            result.push_back(ConstInstruction{base, hi, static_cast<int64_t>((hi64 << 32) | lo),
                                              OperandWidth::Pair});
        }
        else if (op == 0x19) {
            // const-wide/high16 format 21h
            result.push_back(
                ConstInstruction{base, hi, static_cast<int64_t>(static_cast<uint64_t>(unit1) << 48),
                                 OperandWidth::Pair});
        }
        else if (op == 0x1a) {
            result.push_back(ConstStringInstruction{base, hi, unit1});
        }
        else if (op == 0x1b) {
            uint32_t idx = static_cast<uint32_t>(unit1) | (static_cast<uint32_t>(unit2) << 16);
            result.push_back(ConstStringInstruction{base, hi, idx});
        }
        else if (op == 0x1c) {
            result.push_back(ConstClassInstruction{base, hi, unit1});
        }
        else if (op == 0x1d || op == 0x1e) {
            result.push_back(MonitorInstruction{base, hi, op == 0x1d});
        }
        else if (op == 0x1f) {
            // check-cast format 21c: a single in-place register, no separate src.
            result.push_back(TypeInstruction{base, unit1, hi, std::nullopt});
        }
        else if (op == 0x20) {
            // instance-of format 22c: A|B op CCCC → dest=A, src=B
            result.push_back(TypeInstruction{base, unit1, static_cast<uint8_t>(hi & 0x0f),
                                             static_cast<uint8_t>((hi >> 4) & 0x0f)});
        }
        else if (op == 0x21) {
            // array-length format 12x
            result.push_back(UnaryOpInstruction{base, static_cast<uint8_t>(hi & 0x0f),
                                                static_cast<uint8_t>((hi >> 4) & 0x0f),
                                                OperandWidth::Single, OperandWidth::Single});
        }
        else if (op == 0x22) {
            // new-instance format 21c
            result.push_back(TypeInstruction{base, unit1, hi, std::nullopt});
        }
        else if (op == 0x23) {
            // new-array format 22c: A|B op CCCC → dest=A, size_reg=B
            result.push_back(TypeInstruction{base, unit1, static_cast<uint8_t>(hi & 0x0f),
                                             static_cast<uint8_t>((hi >> 4) & 0x0f)});
        }
        else if (op == 0x24) {
            // filled-new-array format 35c: element registers + array type.
            result.push_back(FilledNewArrayInstruction{base, unit1, decode_35c_regs(unit0, unit2)});
        }
        else if (op == 0x25) {
            // filled-new-array/range format 3rc: element register range + array type.
            result.push_back(FilledNewArrayInstruction{base, unit1, decode_3rc_regs(unit0, unit2)});
        }
        else if (op == 0x26) {
            // fill-array-data format 31t
            int32_t off = static_cast<int32_t>(static_cast<uint32_t>(unit1) |
                                               (static_cast<uint32_t>(unit2) << 16));
            result.push_back(FillArrayInstruction{base, hi, off});
        }
        else if (op == 0x27) {
            result.push_back(ThrowInstruction{base, hi});
        }
        else if (op == 0x28) {
            // goto format 10t: signed 8-bit offset in hi byte
            result.push_back(GotoInstruction{base, sex8(hi)});
        }
        else if (op == 0x29) {
            result.push_back(GotoInstruction{base, sex16(unit1)});
        }
        else if (op == 0x2a) {
            int32_t off = static_cast<int32_t>(static_cast<uint32_t>(unit1) |
                                               (static_cast<uint32_t>(unit2) << 16));
            result.push_back(GotoInstruction{base, off});
        }
        else if (op == 0x2b || op == 0x2c) {
            // packed-switch / sparse-switch format 31t
            int32_t off = static_cast<int32_t>(static_cast<uint32_t>(unit1) |
                                               (static_cast<uint32_t>(unit2) << 16));
            SwitchInstruction sw{base, off, hi, /*is_packed=*/op == 0x2b, {}};
            decode_switch_payload(insns, base.offset, off, sw);
            result.push_back(std::move(sw));
        }
        else if (op >= 0x2d && op <= 0x31) {
            // format 23x: AA op CC BB → dest=AA, src_a=BB, src_b=CC
            OperandWidth sw = (op >= 0x2f) ? OperandWidth::Pair : OperandWidth::Single;
            result.push_back(CompareInstruction{base, hi, static_cast<uint8_t>(unit1 & 0xff),
                                                static_cast<uint8_t>(unit1 >> 8), sw});
        }
        else if (op >= 0x32 && op <= 0x37) {
            // if-* format 22t: A|B op CCCC
            result.push_back(IfInstruction{base, sex16(unit1), static_cast<uint8_t>(hi & 0x0f),
                                           static_cast<uint8_t>((hi >> 4) & 0x0f)});
        }
        else if (op >= 0x38 && op <= 0x3d) {
            // if-*z format 21t: AA op BBBB
            result.push_back(IfZeroInstruction{base, sex16(unit1), hi});
        }
        else if (op >= 0x44 && op <= 0x51) {
            // aget/aput format 23x: AA op CC BB
            OperandWidth width =
                (op == 0x45 || op == 0x4c) ? OperandWidth::Pair : OperandWidth::Single;
            result.push_back(ArrayOpInstruction{base, hi, static_cast<uint8_t>(unit1 & 0xff),
                                                static_cast<uint8_t>(unit1 >> 8), op >= 0x4b,
                                                width});
        }
        else if (op >= 0x52 && op <= 0x5f) {
            // iget/iput format 22c: A|B op CCCC
            OperandWidth width =
                (op == 0x53 || op == 0x5a) ? OperandWidth::Pair : OperandWidth::Single;
            result.push_back(FieldInstruction{base, unit1, static_cast<uint8_t>(hi & 0x0f),
                                              static_cast<uint8_t>((hi >> 4) & 0x0f), false,
                                              op >= 0x59, width});
        }
        else if (op >= 0x60 && op <= 0x6d) {
            // sget/sput format 21c: AA op BBBB
            OperandWidth width =
                (op == 0x61 || op == 0x68) ? OperandWidth::Pair : OperandWidth::Single;
            result.push_back(
                FieldInstruction{base, unit1, hi, std::nullopt, true, op >= 0x67, width});
        }
        else if (op >= 0x6e && op <= 0x72) {
            // invoke-* format 35c
            result.push_back(InvokeInstruction{base, unit1, decode_35c_regs(unit0, unit2)});
        }
        else if (op >= 0x74 && op <= 0x78) {
            // invoke-*/range format 3rc
            result.push_back(InvokeInstruction{base, unit1, decode_3rc_regs(unit0, unit2)});
        }
        else if (op >= 0x7b && op <= 0x8f) {
            // unary ops format 12x: B|A op → dest=A, src=B
            uint8_t dest = static_cast<uint8_t>(hi & 0x0f);
            uint8_t src = static_cast<uint8_t>((hi >> 4) & 0x0f);
            OperandWidth dest_w, src_w;
            switch (op) {
            case 0x7b:
                dest_w = OperandWidth::Single;
                src_w = OperandWidth::Single;
                break;
            case 0x7c:
                dest_w = OperandWidth::Single;
                src_w = OperandWidth::Single;
                break;
            case 0x7d:
                dest_w = OperandWidth::Pair;
                src_w = OperandWidth::Pair;
                break;
            case 0x7e:
                dest_w = OperandWidth::Pair;
                src_w = OperandWidth::Pair;
                break;
            case 0x7f:
                dest_w = OperandWidth::Single;
                src_w = OperandWidth::Single;
                break;
            case 0x80:
                dest_w = OperandWidth::Pair;
                src_w = OperandWidth::Pair;
                break;
            case 0x81:
                dest_w = OperandWidth::Pair;
                src_w = OperandWidth::Single;
                break;
            case 0x82:
                dest_w = OperandWidth::Single;
                src_w = OperandWidth::Single;
                break;
            case 0x83:
                dest_w = OperandWidth::Pair;
                src_w = OperandWidth::Single;
                break;
            case 0x84:
                dest_w = OperandWidth::Single;
                src_w = OperandWidth::Pair;
                break;
            case 0x85:
                dest_w = OperandWidth::Single;
                src_w = OperandWidth::Pair;
                break;
            case 0x86:
                dest_w = OperandWidth::Pair;
                src_w = OperandWidth::Pair;
                break;
            case 0x87:
                dest_w = OperandWidth::Single;
                src_w = OperandWidth::Single;
                break;
            case 0x88:
                dest_w = OperandWidth::Pair;
                src_w = OperandWidth::Single;
                break;
            case 0x89:
                dest_w = OperandWidth::Pair;
                src_w = OperandWidth::Single;
                break;
            case 0x8a:
                dest_w = OperandWidth::Single;
                src_w = OperandWidth::Pair;
                break;
            case 0x8b:
                dest_w = OperandWidth::Pair;
                src_w = OperandWidth::Pair;
                break;
            case 0x8c:
                dest_w = OperandWidth::Single;
                src_w = OperandWidth::Pair;
                break;
            default:
                dest_w = OperandWidth::Single;
                src_w = OperandWidth::Single;
                break;
            }
            result.push_back(UnaryOpInstruction{base, dest, src, dest_w, src_w});
        }
        else if (op >= 0x90 && op <= 0xaf) {
            // binary ops format 23x: AA op CC BB
            result.push_back(BinaryOpInstruction{base, hi, static_cast<uint8_t>(unit1 & 0xff),
                                                 static_cast<uint8_t>(unit1 >> 8),
                                                 binary_op_width(op)});
        }
        else if (op >= 0xb0 && op <= 0xcf) {
            // binary /2addr format 12x: B|A op → dest=src_a=A, src_b=B
            uint8_t dest = static_cast<uint8_t>(hi & 0x0f);
            result.push_back(BinaryOpInstruction{
                base, dest, dest, static_cast<uint8_t>((hi >> 4) & 0x0f), binary_op_width(op)});
        }
        else if (op >= 0xd0 && op <= 0xd7) {
            // binary lit16 format 22s: A|B op CCCC
            result.push_back(BinaryLitInstruction{base, static_cast<uint8_t>(hi & 0x0f),
                                                  static_cast<uint8_t>((hi >> 4) & 0x0f),
                                                  static_cast<int16_t>(unit1)});
        }
        else if (op >= 0xd8 && op <= 0xe2) {
            // binary lit8 format 22b: AA op BB CC
            result.push_back(
                BinaryLitInstruction{base, hi, static_cast<uint8_t>(unit1 & 0xff),
                                     static_cast<int16_t>(sex8(static_cast<uint8_t>(unit1 >> 8)))});
        }
        else if (op == 0xfa) {
            // invoke-polymorphic format 45cc
            result.push_back(InvokeInstruction{base, unit1, decode_35c_regs(unit0, unit2)});
        }
        else if (op == 0xfb) {
            // invoke-polymorphic/range format 4rcc
            result.push_back(InvokeInstruction{base, unit1, decode_3rc_regs(unit0, unit2)});
        }
        else if (op == 0xfc) {
            // invoke-custom format 35c
            result.push_back(InvokeCustomInstruction{base, unit1, decode_35c_regs(unit0, unit2)});
        }
        else if (op == 0xfd) {
            // invoke-custom/range format 3rc
            result.push_back(InvokeCustomInstruction{base, unit1, decode_3rc_regs(unit0, unit2)});
        }
        else if (op == 0xfe || op == 0xff) {
            // const-method-handle / const-method-type format 21c
            result.push_back(ConstHandleInstruction{base, hi, unit1});
        }
        else {
            // Unknown/unused opcode — emit nop to keep the stream contiguous
            result.push_back(NopInstruction{base});
        }

        pos += sz;
    }

    return result;
}

} // namespace dex
