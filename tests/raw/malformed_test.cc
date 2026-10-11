// Regression tests for parser hardening against malformed / adversarial input.
// These cover the memory-safety findings from the security review (F1-F3): the
// parser must never read out of bounds on a crafted DEX, and the analysis layer
// must degrade to empty results rather than dereferencing out-of-range pool
// indices. Run under -fsanitize=address in CI to catch regressions.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include "dex.hpp"
#include "raw/parser.hpp"
#include "raw/parsing_utils.hpp"
#include "test_utils.hpp"

using namespace dex;

namespace {

// DEX header byte offsets of the section-offset fields.
constexpr std::size_t MAP_OFF = 0x34;
constexpr std::size_t STRING_IDS_OFF = 0x3C;
constexpr std::size_t TYPE_IDS_OFF = 0x44;
constexpr std::size_t PROTO_IDS_OFF = 0x4C;
constexpr std::size_t FIELD_IDS_OFF = 0x54;
constexpr std::size_t METHOD_IDS_OFF = 0x5C;
constexpr std::size_t CLASS_DEFS_OFF = 0x64;
constexpr std::size_t DATA_OFF = 0x6C;

void put_u32(std::vector<uint8_t> &b, std::size_t off, uint32_t v)
{
    b[off] = v & 0xFF;
    b[off + 1] = (v >> 8) & 0xFF;
    b[off + 2] = (v >> 16) & 0xFF;
    b[off + 3] = (v >> 24) & 0xFF;
}

uint32_t get_u32(const std::vector<uint8_t> &b, std::size_t off)
{
    return b[off] | (b[off + 1] << 8) | (b[off + 2] << 16) | (b[off + 3] << 24);
}

// Write bytes to a unique temp file and return its path (caller removes it).
std::string write_temp(const std::vector<uint8_t> &bytes, const char *tag)
{
    auto path = std::filesystem::temp_directory_path() /
                ("dexpp_malformed_" + std::string(tag) + "_" +
                 std::to_string(reinterpret_cast<uintptr_t>(bytes.data())) + ".dex");
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return path.string();
}

} // namespace

// F1: a section offset near UINT32_MAX must not overflow the bounds check into
// a wild read — every such field must yield a parse error, not a crash.
TEST(MalformedTest, HeaderOffsetNearUintMaxDoesNotOverflow)
{
    const std::size_t fields[] = {MAP_OFF,       STRING_IDS_OFF, TYPE_IDS_OFF,   PROTO_IDS_OFF,
                                  FIELD_IDS_OFF, METHOD_IDS_OFF, CLASS_DEFS_OFF, DATA_OFF};
    for (std::size_t field : fields) {
        auto buf = load_buf("tests/data/classes.dex");
        put_u32(buf, field, 0xFFFFFFFE);
        // Must return (error or, for unused sections, success) WITHOUT crashing.
        auto r = raw::parser::parse_buffer(std::span<const uint8_t>(buf), {});
        SUCCEED() << "field 0x" << std::hex << field << (r.has_value() ? " parsed" : " rejected");
    }
}

// F1: the raw read helpers must reject an offset that would overflow.
TEST(MalformedTest, ReadHelpersRejectOverflowingOffset)
{
    std::vector<uint8_t> buf(16, 0);
    uint32_t off = 0xFFFFFFFE;
    EXPECT_FALSE(raw::parse_utils::read_uint32(std::span<const uint8_t>(buf), off).has_value());
    off = 0xFFFFFFFF;
    EXPECT_FALSE(raw::parse_utils::read_uint16(std::span<const uint8_t>(buf), off).has_value());
}

// F2: a 4-byte MUTF-8 lead with fewer than 3 trailing bytes must error, not
// read one past the buffer end.
TEST(MalformedTest, Mutf8FourByteAtBoundary)
{
    std::vector<uint8_t> b = {0xF0, 0x80, 0x80}; // needs a 4th byte; none present
    uint32_t off = 0;
    auto r = raw::parse_utils::decode_mutf8(std::span<const uint8_t>(b), off);
    EXPECT_FALSE(r.has_value());

    std::vector<uint8_t> b3 = {0xE0, 0x80}; // 3-byte lead, one byte short
    off = 0;
    EXPECT_FALSE(raw::parse_utils::decode_mutf8(std::span<const uint8_t>(b3), off).has_value());
}

// Truncating a valid DEX at every length must never crash the parser.
TEST(MalformedTest, TruncationNeverCrashes)
{
    auto full = load_buf("tests/data/classes.dex");
    for (std::size_t len = 0; len < full.size(); len += 17) {
        std::vector<uint8_t> trunc(full.begin(), full.begin() + len);
        auto r = raw::parser::parse_buffer(std::span<const uint8_t>(trunc), {});
        (void)r; // only asserting "no crash / no OOB"
    }
    SUCCEED();
}

// F3: an out-of-range class_idx must make Class::name() return empty, not read
// past the type-id table.
TEST(MalformedTest, OutOfRangeClassIdxYieldsEmptyName)
{
    auto buf = load_buf("tests/data/classes.dex");
    uint32_t class_defs_off = get_u32(buf, CLASS_DEFS_OFF);
    ASSERT_LT(class_defs_off + 4, buf.size());
    // ClassDefItem.class_idx is the first u32 of the item.
    put_u32(buf, class_defs_off, 0xFFFFFFFF);

    auto path = write_temp(buf, "classidx");
    auto ctx = AnalysisContext::from_dex(path);
    ASSERT_TRUE(ctx.has_value()) << ctx.error().message;

    auto classes = ctx->classes();
    ASSERT_FALSE(classes.empty());
    for (const auto &c : classes) {
        auto name = c.name(); // must not OOB-read
        EXPECT_TRUE(name.empty() || name.front() == 'L');
        // Touching members must also be safe.
        for (const auto &m : c.methods()) {
            (void)m.name();
            (void)m.return_type().descriptor();
        }
        for (const auto &f : c.fields())
            (void)f.name();
    }
    std::filesystem::remove(path);
}

// F3: whole-program products must tolerate the same corruption.
TEST(MalformedTest, WholeProgramAnalysesTolerateCorruption)
{
    auto buf = load_buf("tests/data/classes.dex");
    uint32_t class_defs_off = get_u32(buf, CLASS_DEFS_OFF);
    put_u32(buf, class_defs_off, 0xFFFFFFFF);

    auto path = write_temp(buf, "wholeprog");
    auto ctx = AnalysisContext::from_dex(path);
    ASSERT_TRUE(ctx.has_value());

    EXPECT_NO_FATAL_FAILURE({
        (void)ctx->call_graph().nodes().size();
        (void)ctx->class_hierarchy().empty();
        (void)ctx->xrefs().empty();
        (void)ctx->strings().size();
    });
    std::filesystem::remove(path);
}

// F4: an attacker-controlled element count (ULEB128 in class_data / code_item,
// u32 in the annotations directory) must not drive an unbounded reserve() — a
// few bytes encoding 0xFFFFFFFF would otherwise trigger a multi-gigabyte
// speculative allocation before any element is read. These parse a tiny buffer
// whose count claims ~4 billion elements: pre-fix each throws std::bad_alloc
// out of reserve() (the harness would crash); post-fix the reserve is clamped
// to what the buffer can hold and the read loop returns a truncation error.
namespace {
void put_uleb_max(std::vector<uint8_t> &b) // ULEB128 of 0xFFFFFFFF
{
    b.insert(b.end(), {0xFF, 0xFF, 0xFF, 0xFF, 0x0F});
}
} // namespace

TEST(MalformedTest, ClassDataHugeMethodCountDoesNotOverAllocate)
{
    std::vector<uint8_t> buf;
    buf.push_back(0x00); // static_fields_size = 0
    buf.push_back(0x00); // instance_fields_size = 0
    put_uleb_max(buf);   // direct_methods_size = 0xFFFFFFFF
    buf.push_back(0x00); // virtual_methods_size = 0

    auto r = raw::parser::parse_class_data(buf, 0);
    EXPECT_FALSE(r.has_value()) << "a 4-billion method count in an 8-byte buffer must fail";
}

TEST(MalformedTest, AnnotationsDirectoryHugeFieldCountDoesNotOverAllocate)
{
    std::vector<uint8_t> buf(16, 0x00); // class_off + 3 size fields, min chunk
    put_u32(buf, 4, 0xFFFFFFFF);        // fields_size = 0xFFFFFFFF

    auto r = raw::parser::parse_annotations_directory(buf, 0);
    EXPECT_FALSE(r.has_value());
}

TEST(MalformedTest, CodeItemHugeCatchHandlerListDoesNotOverAllocate)
{
    std::vector<uint8_t> buf;
    auto put_u16 = [&](uint16_t v) { buf.insert(buf.end(), {uint8_t(v), uint8_t(v >> 8)}); };
    put_u16(0);                     // registers_size
    put_u16(0);                     // ins_size
    put_u16(0);                     // outs_size
    put_u16(1);                     // tries_size = 1 (so the handler list is reached)
    buf.insert(buf.end(), 4, 0x00); // debug_info_off = 0
    buf.insert(buf.end(), 4, 0x00); // insns_size = 0
    buf.insert(buf.end(), 8, 0x00); // one try_item (start_addr, insn_count, handler_off)
    put_uleb_max(buf);              // encoded_catch_handler_list size = 0xFFFFFFFF

    auto r = raw::parser::parse_code_item(buf, 0);
    EXPECT_FALSE(r.has_value());
}
