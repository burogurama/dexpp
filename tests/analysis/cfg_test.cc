#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

#include "../raw/test_utils.hpp"
#include "analysis/cfg.hpp"
#include "dex.hpp"
#include "raw/parser.hpp"
#include "raw/types.hpp"

using namespace dex;
using dex::raw::CodeItem;

namespace {

// Build a Cfg from a synthetic insns vector with no try/catch blocks.
Cfg build_synthetic(std::vector<uint16_t> insns)
{
    CodeItem code{};
    code.insns = std::move(insns);
    auto decoded = decode_instructions(std::span<const uint16_t>(code.insns));
    return build_cfg(code, decoded);
}

inline void push_s32(std::vector<uint16_t> &v, int32_t x)
{
    auto u = static_cast<uint32_t>(x);
    v.push_back(static_cast<uint16_t>(u & 0xffff));
    v.push_back(static_cast<uint16_t>((u >> 16) & 0xffff));
}

} // namespace

// ===== Synthetic tests =====

TEST(CfgTest, EmptyCodeItemEmptyCfg)
{
    CodeItem code{};
    auto decoded = decode_instructions(std::span<const uint16_t>(code.insns));
    auto cfg = build_cfg(code, decoded);
    EXPECT_TRUE(cfg.empty());
}

TEST(CfgTest, SingleReturnVoidOneBlock)
{
    auto cfg = build_synthetic({0x000e}); // return-void
    ASSERT_EQ(cfg.blocks().size(), 1u);
    EXPECT_EQ(cfg.entry().start_offset, 0u);
    EXPECT_EQ(cfg.entry().end_offset, 1u);
    EXPECT_EQ(cfg.entry().insn_count, 1u);
    EXPECT_TRUE(cfg.entry().successors.empty());
    EXPECT_TRUE(cfg.entry().predecessors.empty());
}

TEST(CfgTest, SwitchCasesAndDefault)
{
    // Layout:
    //   pos 0: packed-switch v0, +9     (3 units)
    //   pos 3: return-void              (1)  ← default fall-through
    //   pos 4: nop                      (1)
    //   pos 5: nop                      (1)  ← case 0 target
    //   pos 6: nop                      (1)
    //   pos 7: nop                      (1)  ← case 1 target
    //   pos 8: return-void              (1)
    //   pos 9: packed-switch-payload    (8)  size=2 first_key=10 targets=+5,+7
    std::vector<uint16_t> insns;
    insns.push_back(static_cast<uint16_t>(0x2b | (0 << 8))); // packed-switch v0
    push_s32(insns, 9);                                      // payload_offset = +9
    insns.push_back(0x000e);                                 // return-void (default)
    insns.push_back(0x0000);                                 // nop
    insns.push_back(0x0000);                                 // nop (case 0 target)
    insns.push_back(0x0000);                                 // nop
    insns.push_back(0x0000);                                 // nop (case 1 target)
    insns.push_back(0x000e);                                 // return-void
    insns.push_back(0x0100);                                 // payload ident
    insns.push_back(2);                                      // size = 2
    push_s32(insns, 10);                                     // first_key = 10
    push_s32(insns, 5);                                      // target 0 (relative): +5
    push_s32(insns, 7);                                      // target 1 (relative): +7

    auto cfg = build_synthetic(std::move(insns));
    ASSERT_FALSE(cfg.empty());

    const auto &entry = cfg.entry();
    EXPECT_EQ(entry.start_offset, 0u);

    // Three normal-flow successors out of the switch block.
    int n_case = 0, n_default = 0;
    std::vector<int64_t> case_keys;
    for (const auto &e : entry.successors) {
        if (e.kind == EdgeKind::SwitchCase) {
            ++n_case;
            ASSERT_TRUE(e.label.has_value());
            case_keys.push_back(*e.label);
        }
        else if (e.kind == EdgeKind::SwitchDefault) {
            ++n_default;
        }
    }
    EXPECT_EQ(n_case, 2);
    EXPECT_EQ(n_default, 1);
    std::sort(case_keys.begin(), case_keys.end());
    EXPECT_EQ(case_keys, (std::vector<int64_t>{10, 11}));

    // SwitchCase targets must be the blocks at offsets 5 and 7.
    const auto *blk5 = cfg.block_at_offset(5);
    const auto *blk7 = cfg.block_at_offset(7);
    ASSERT_NE(blk5, nullptr);
    ASSERT_NE(blk7, nullptr);
}

TEST(CfgTest, GotoEndsBlockEvenWhenFollowedByDeadCode)
{
    // Regression: the goto's block must end at the goto, even when the next
    // instruction is unreachable from any other branch.
    //   pos 0: goto +3                                    (1 unit)
    //   pos 1: nop  (unreachable; no other branch targets this offset)
    //   pos 2: nop  (unreachable)
    //   pos 3: return-void                                ← branch target
    auto cfg = build_synthetic({
        0x0328, // op=0x28 goto, hi byte = signed 8-bit offset = +3
        0x0000, // nop  (dead)
        0x0000, // nop  (dead)
        0x000e, // return-void
    });
    ASSERT_FALSE(cfg.empty());

    const BasicBlock *entry = cfg.block_at_offset(0);
    ASSERT_NE(entry, nullptr);

    // The goto block must be exactly one instruction long, and its only
    // outgoing edge must be the Goto edge to offset 3.
    EXPECT_EQ(entry->insn_count, 1u);
    ASSERT_EQ(entry->successors.size(), 1u);
    EXPECT_EQ(entry->successors[0].kind, EdgeKind::Goto);

    const BasicBlock *target = cfg.block_at_offset(3);
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(entry->successors[0].target_block, target->index);
}

TEST(CfgTest, ThrowInTryNoFallthrough)
{
    // Synthetic CodeItem with a single throw covered by a catch-all.
    //   pos 0: throw v0  (op=0x27 hi=00 → throw v0)         (1 unit)
    //   pos 1: return-void                                  (1 unit)  ← handler entry
    CodeItem code{};
    code.insns = {0x0027, 0x000e};

    raw::TryItem t{};
    t.start_addr = 0;
    t.insn_count = 1;
    t.handler_off = 0;
    code.tries.push_back(t);

    raw::EncodedCatchHandler h{};
    h.catch_all_addr = 1u;
    code.handlers.push_back(std::move(h));
    code.handler_byte_offsets.push_back(0);

    auto decoded = decode_instructions(std::span<const uint16_t>(code.insns));
    auto cfg = build_cfg(code, decoded);
    ASSERT_FALSE(cfg.empty());

    const BasicBlock *throw_blk = cfg.block_at_offset(0);
    ASSERT_NE(throw_blk, nullptr);

    // Throw block has no normal successors — only the Exception edge.
    ASSERT_EQ(throw_blk->successors.size(), 1u);
    EXPECT_EQ(throw_blk->successors[0].kind, EdgeKind::Exception);

    const BasicBlock *handler_blk = cfg.block_at_offset(1);
    ASSERT_NE(handler_blk, nullptr);
    EXPECT_TRUE(handler_blk->is_handler_entry);
}

// ===== Real-fixture tests on trycatch.dex =====

namespace {

// Decode and build the CFG for the method whose code_item starts at @p code_off
// in trycatch.dex.  Returns std::nullopt on parse failure.
struct CfgBundle
{
    CodeItem code;
    std::vector<Instruction> decoded;
    Cfg cfg;
};

CfgBundle load_trycatch_cfg(uint32_t code_off)
{
    auto buf = load_buf("tests/data/trycatch.dex");
    auto ci = raw::parser::parse_code_item(std::span(buf), code_off);
    EXPECT_TRUE(ci.has_value()) << ci.error().message;

    CfgBundle out;
    out.code = std::move(*ci);
    out.decoded = decode_instructions(std::span<const uint16_t>(out.code.insns));
    out.cfg = build_cfg(out.code, std::span<const Instruction>(out.decoded));
    return out;
}

} // namespace

TEST(CfgTest, IfHasTakenAndNotTaken)
{
    // Synthetic: if-eqz v0, +3 ; nop ; return-void
    //   pos 0: if-eqz v0, +3 (op=0x38, 2 units)
    //   pos 2: nop           (1 unit)
    //   pos 3: return-void   (1 unit)  ← branch target
    auto cfg = build_synthetic({
        0x0038, // op=0x38 if-eqz, reg v0
        0x0003, // branch_offset = +3
        0x0000, // nop
        0x000e, // return-void
    });
    ASSERT_FALSE(cfg.empty());

    const BasicBlock *entry = cfg.block_at_offset(0);
    ASSERT_NE(entry, nullptr);

    int taken = 0, not_taken = 0;
    for (const auto &e : entry->successors) {
        if (e.kind == EdgeKind::BranchTaken)
            ++taken;
        else if (e.kind == EdgeKind::BranchNotTaken)
            ++not_taken;
    }
    EXPECT_EQ(taken, 1);
    EXPECT_EQ(not_taken, 1);

    // Branch target is offset 3 (return); fall-through is offset 2 (nop).
    EXPECT_NE(cfg.block_at_offset(3), nullptr);
    EXPECT_NE(cfg.block_at_offset(2), nullptr);
}

TEST(CfgTest, GotoSingleSuccessor)
{
    // safeParse contains a goto (verified by the existing GotoPresent test).
    auto bundle = load_trycatch_cfg(0x1d0);
    ASSERT_FALSE(bundle.cfg.empty());

    bool found = false;
    for (const auto &blk : bundle.cfg.blocks()) {
        // A block ending in goto has exactly one normal-flow Goto successor;
        // exception edges may add more, but normal flow is just one.
        int normal_edges = 0;
        int goto_edges = 0;
        for (const auto &e : blk.successors) {
            if (e.kind == EdgeKind::Goto)
                ++goto_edges;
            if (e.kind != EdgeKind::Exception)
                ++normal_edges;
        }
        if (goto_edges == 1) {
            EXPECT_EQ(normal_edges, 1) << "Goto block should have exactly one normal successor";
            found = true;
        }
    }
    EXPECT_TRUE(found) << "Expected at least one goto edge in safeParse()";
}

TEST(CfgTest, TryStartIsLeader)
{
    auto bundle = load_trycatch_cfg(0x1a8); // divide
    ASSERT_FALSE(bundle.code.tries.empty());

    for (const auto &t : bundle.code.tries) {
        const BasicBlock *b = bundle.cfg.block_at_offset(t.start_addr);
        EXPECT_NE(b, nullptr) << "No block starts at try.start_addr=0x" << std::hex << t.start_addr;
    }
}

TEST(CfgTest, DivideHasExceptionEdgeToHandler)
{
    auto bundle = load_trycatch_cfg(0x1a8);
    ASSERT_FALSE(bundle.cfg.empty());

    int exception_edges = 0;
    int handler_blocks = 0;
    for (const auto &blk : bundle.cfg.blocks()) {
        for (const auto &e : blk.successors) {
            if (e.kind == EdgeKind::Exception)
                ++exception_edges;
        }
        if (blk.is_handler_entry)
            ++handler_blocks;
    }
    EXPECT_GT(exception_edges, 0);
    EXPECT_GT(handler_blocks, 0);
}

TEST(CfgTest, CfgPointerStableAcrossCalls)
{
    auto ctx = AnalysisContext::from_dex("tests/data/trycatch.dex");
    ASSERT_TRUE(ctx.has_value()) << ctx.error().message;

    auto cls = ctx->find_class("LTryCatch;");
    ASSERT_TRUE(cls.has_value());
    auto methods = cls->methods();
    ASSERT_FALSE(methods.empty());

    // Find a concrete method.
    const Method *concrete = nullptr;
    for (const auto &m : methods) {
        if (!m.instructions().empty()) {
            concrete = &m;
            break;
        }
    }
    ASSERT_NE(concrete, nullptr);

    const Cfg &c1 = concrete->cfg();
    const Cfg &c2 = concrete->cfg();
    EXPECT_EQ(&c1, &c2) << "Method::cfg() must return the same cached reference";
    EXPECT_FALSE(c1.empty());
}

TEST(CfgTest, AbstractMethodEmptyCfg)
{
    auto ctx = AnalysisContext::from_dex("tests/data/shapes.dex");
    ASSERT_TRUE(ctx.has_value()) << ctx.error().message;

    auto cls = ctx->find_class("LShape;");
    ASSERT_TRUE(cls.has_value());

    bool checked_at_least_one = false;
    for (const auto &m : cls->methods()) {
        EXPECT_FALSE(m.has_code()) << "Interface method " << m.name() << " should be abstract";
        EXPECT_TRUE(m.cfg().empty()) << "Abstract method " << m.name() << " must have empty CFG";
        checked_at_least_one = true;
    }
    EXPECT_TRUE(checked_at_least_one);
}

TEST(CfgTest, PredecessorsConsistent)
{
    // For every method in trycatch.dex's class data, every successor edge must
    // have a matching predecessor entry.
    for (uint32_t code_off : {0x23cu, 0x1a8u, 0x1d0u}) {
        auto bundle = load_trycatch_cfg(code_off);
        if (bundle.cfg.empty())
            continue;

        for (const auto &blk : bundle.cfg.blocks()) {
            for (const auto &edge : blk.successors) {
                ASSERT_LT(edge.target_block, bundle.cfg.blocks().size());
                const auto &target = bundle.cfg.blocks()[edge.target_block];
                bool found = std::find(target.predecessors.begin(), target.predecessors.end(),
                                       blk.index) != target.predecessors.end();
                EXPECT_TRUE(found) << "block " << blk.index << " → " << edge.target_block
                                   << " missing in predecessors[" << edge.target_block << "]";
            }
        }
    }
}
