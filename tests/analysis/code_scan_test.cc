// The scanners in analysis/code_scan.hpp must report exactly what
// decode_instructions() reports: the call graph takes its call sites from
// detail::find_invokes() and the cross-references take theirs from
// detail::find_refs(), and both have to match Method::instructions(),
// Method::calls() and the other forward references.

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <span>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "../raw/test_utils.hpp"
#include "analysis/code_scan.hpp"
#include "dex.hpp"
#include "raw/parser.hpp"

using namespace dex;

namespace {

std::vector<detail::InvokeSite> invokes_from_decoder(std::span<const uint16_t> insns)
{
    std::vector<detail::InvokeSite> out;
    for (const auto &insn : decode_instructions(insns)) {
        std::visit(
            [&](const auto &i) {
                using T = std::decay_t<decltype(i)>;
                if constexpr (std::is_same_v<T, InvokeInstruction> ||
                              std::is_same_v<T, InvokePolymorphicInstruction>)
                    out.push_back(
                        {i.base.offset, i.base.opcode, static_cast<uint16_t>(i.method_index)});
                else if constexpr (std::is_same_v<T, InvokeCustomInstruction>)
                    out.push_back(
                        {i.base.offset, i.base.opcode, static_cast<uint16_t>(i.call_site_index)});
            },
            insn);
    }
    return out;
}

// The instructions the cross-reference index records, as the decoder sees them.
std::vector<detail::RefSite> refs_from_decoder(std::span<const uint16_t> insns)
{
    std::vector<detail::RefSite> out;
    for (const auto &insn : decode_instructions(insns)) {
        std::visit(
            [&](const auto &i) {
                using T = std::decay_t<decltype(i)>;
                if constexpr (std::is_same_v<T, ConstStringInstruction>)
                    out.push_back({i.base.offset, i.string_index, i.base.opcode});
                else if constexpr (std::is_same_v<T, ConstClassInstruction> ||
                                   std::is_same_v<T, TypeInstruction> ||
                                   std::is_same_v<T, FilledNewArrayInstruction>)
                    out.push_back({i.base.offset, i.type_index, i.base.opcode});
                else if constexpr (std::is_same_v<T, FieldInstruction>)
                    out.push_back({i.base.offset, i.field_index, i.base.opcode});
                else if constexpr (std::is_same_v<T, InvokeInstruction> ||
                                   std::is_same_v<T, InvokePolymorphicInstruction>)
                    out.push_back({i.base.offset, i.method_index, i.base.opcode});
            },
            insn);
    }
    return out;
}

// Compares the two and returns how many invokes they found.
std::size_t expect_same_invokes(std::span<const uint16_t> insns, const std::string &where)
{
    std::vector<detail::InvokeSite> scanned;
    detail::find_invokes(insns, scanned);
    auto decoded = invokes_from_decoder(insns);
    EXPECT_EQ(scanned.size(), decoded.size()) << where;
    for (std::size_t k = 0; k < std::min(scanned.size(), decoded.size()); ++k) {
        EXPECT_EQ(scanned[k].offset, decoded[k].offset) << where << " invoke " << k;
        EXPECT_EQ(scanned[k].opcode, decoded[k].opcode) << where << " invoke " << k;
        EXPECT_EQ(scanned[k].index, decoded[k].index) << where << " invoke " << k;
    }
    return decoded.size();
}

// Compares the two and returns how many references they found.
std::size_t expect_same_refs(std::span<const uint16_t> insns, const std::string &where)
{
    std::vector<detail::RefSite> scanned;
    detail::find_refs(insns, scanned);
    auto decoded = refs_from_decoder(insns);
    EXPECT_EQ(scanned.size(), decoded.size()) << where;
    for (std::size_t k = 0; k < std::min(scanned.size(), decoded.size()); ++k) {
        EXPECT_EQ(scanned[k].offset, decoded[k].offset) << where << " reference " << k;
        EXPECT_EQ(scanned[k].opcode, decoded[k].opcode) << where << " reference " << k;
        EXPECT_EQ(scanned[k].index, decoded[k].index) << where << " reference " << k;
    }
    return decoded.size();
}

} // namespace

TEST(CodeScanTest, MatchesDecoderOnRealCode)
{
    std::size_t invokes = 0;
    std::size_t refs = 0;
    for (const char *path :
         {"tests/data/classes.dex", "tests/data/polymorphic.dex", "tests/data/inherit.dex",
          "tests/data/trycatch.dex", "tests/data/shapes.dex", "tests/data/fibonacci.dex",
          "tests/data/handles.dex", "tests/data/casts.dex", "tests/data/multistring.dex",
          "tests/data/switches.dex", "tests/data/local/AnalysisTest.dex",
          "tests/data/local/ExceptionHandling.dex", "tests/data/local/StringTests.dex",
          "tests/data/local/classes.dex", "tests/data/local/InterfaceCls.dex",
          "tests/data/local/FieldsTest.dex", "tests/data/local/Annotation_classes.dex"}) {
        if (missing_local_fixture(path))
            continue;
        auto buf = load_buf(path);
        auto dex = raw::parser::parse_buffer(std::span(buf));
        ASSERT_TRUE(dex.has_value()) << path;
        for (const auto &cdef : dex->class_defs()) {
            if (cdef.class_data_off == 0)
                continue;
            auto cd = raw::parser::parse_class_data(std::span(buf), cdef.class_data_off);
            ASSERT_TRUE(cd.has_value()) << path;
            for (const auto *methods : {&cd->direct_methods, &cd->virtual_methods})
                for (const auto &em : *methods) {
                    if (em.code_off == 0)
                        continue;
                    auto ci = raw::parser::parse_code_item(std::span(buf), em.code_off);
                    ASSERT_TRUE(ci.has_value()) << path;
                    std::string where = std::string(path) + " code@" + std::to_string(em.code_off);
                    invokes += expect_same_invokes(ci->insns, where);
                    refs += expect_same_refs(ci->insns, where);
                }
        }
    }
    EXPECT_GT(invokes, 50u) << "the fixtures should contain invokes";
    EXPECT_GT(refs, invokes) << "the fixtures should contain other references too";
}

TEST(CodeScanTest, MatchesDecoderOnArbitraryCode)
{
    // Random code units, biased towards what moves instruction boundaries or
    // holds a reference: invoke opcodes (every format), the string, type and
    // field instructions (const-string/jumbo is the one 3-unit form), payload
    // pseudo-instructions with small and huge declared sizes, and truncation
    // at the end of the array.
    std::mt19937 rng(20261014);
    std::uniform_int_distribution<int> unit(0, 0xffff);
    std::uniform_int_distribution<int> pick(0, 9);
    const uint16_t interesting[] = {0x006e, 0x0274, 0x0072, 0x0578, 0x10fa, 0x03fb, 0x01fc,
                                    0x04fd, 0x0100, 0x0200, 0x0300, 0x0000, 0x002b, 0x0026,
                                    0x001a, 0x021b, 0x001c, 0x011f, 0x2120, 0x0022, 0x1023,
                                    0x2124, 0x0325, 0x2152, 0x0160, 0x046d, 0xfe00, 0x00ff};
    std::size_t invokes = 0;
    std::size_t refs = 0;
    for (int round = 0; round < 20000; ++round) {
        std::vector<uint16_t> insns(static_cast<std::size_t>(rng() % 48));
        for (auto &u : insns) {
            int p = pick(rng);
            if (p < 4)
                u = interesting[rng() % std::size(interesting)];
            else if (p < 6)
                u = static_cast<uint16_t>(rng() % 8); // small payload sizes / indices
            else
                u = static_cast<uint16_t>(unit(rng));
        }
        std::string where = "round " + std::to_string(round);
        invokes += expect_same_invokes(insns, where);
        refs += expect_same_refs(insns, where);
        if (HasFailure())
            break;
    }
    EXPECT_GT(invokes, 1000u);
    EXPECT_GT(refs, 5000u);
}
