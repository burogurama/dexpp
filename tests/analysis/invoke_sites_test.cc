// detail::find_invokes() must report exactly the invokes decode_instructions()
// reports: the call graph takes its call sites from it, and they have to match
// Method::instructions() / Method::calls().

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <span>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "../raw/test_utils.hpp"
#include "analysis/invoke_sites.hpp"
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
                if constexpr (std::is_same_v<T, InvokeInstruction>)
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

} // namespace

TEST(InvokeSitesTest, MatchesDecoderOnRealCode)
{
    std::size_t invokes = 0;
    for (const char *path :
         {"tests/data/classes.dex", "tests/data/polymorphic.dex", "tests/data/inherit.dex",
          "tests/data/trycatch.dex", "tests/data/shapes.dex", "tests/data/fibonacci.dex",
          "tests/data/androguard/AnalysisTest.dex", "tests/data/androguard/ExceptionHandling.dex",
          "tests/data/androguard/StringTests.dex", "tests/data/androguard/classes.dex",
          "tests/data/androguard/InterfaceCls.dex", "tests/data/androguard/FieldsTest.dex",
          "tests/data/androguard/Annotation_classes.dex"}) {
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
                    invokes += expect_same_invokes(ci->insns, std::string(path) + " code@" +
                                                                  std::to_string(em.code_off));
                }
        }
    }
    EXPECT_GT(invokes, 50u) << "the fixtures should contain invokes";
}

TEST(InvokeSitesTest, MatchesDecoderOnArbitraryCode)
{
    // Random code units, biased towards what moves instruction boundaries:
    // invoke opcodes (every format), payload pseudo-instructions with small and
    // huge declared sizes, and truncation at the end of the array.
    std::mt19937 rng(20261014);
    std::uniform_int_distribution<int> unit(0, 0xffff);
    std::uniform_int_distribution<int> pick(0, 9);
    const uint16_t interesting[] = {0x006e, 0x0274, 0x0072, 0x0578, 0x10fa, 0x03fb, 0x01fc,
                                    0x04fd, 0x0100, 0x0200, 0x0300, 0x0000, 0x002b, 0x0026};
    std::size_t invokes = 0;
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
        invokes += expect_same_invokes(insns, "round " + std::to_string(round));
        if (HasFailure())
            break;
    }
    EXPECT_GT(invokes, 1000u);
}
