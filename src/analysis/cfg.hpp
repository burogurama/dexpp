#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "instruction.hpp"
#include "raw/types.hpp"

namespace dex {

/** Kind of edge between two basic blocks. */
enum class EdgeKind : uint8_t {
    Fallthrough,    ///< Straight-line continuation; only when the predecessor's last instruction
                    ///< is not a terminator.
    Goto,           ///< Unconditional goto / goto/16 / goto/32.
    BranchTaken,    ///< if-* / if-*z taken edge.
    BranchNotTaken, ///< if-* / if-*z fall-through edge.
    SwitchCase,     ///< Outgoing edge for one packed/sparse case (label = case key).
    SwitchDefault,  ///< Outgoing edge for switch fall-through (the "default" case).
    Exception,      ///< try-region → handler edge (label = caught type_idx, or
                    ///< UINT32_MAX for catch-all).
};

/** A single outgoing edge from one basic block to another. */
struct CfgEdge
{
    uint32_t target_block;
    EdgeKind kind;
    /** Optional context for the edge.  SwitchCase: the case key.
     *  Exception: the caught `type_idx`, or UINT32_MAX for a catch-all
     *  handler.  Recover with `static_cast<uint32_t>(*label)` — the
     *  uint32 → int64 → uint32 round-trip preserves the bit pattern,
     *  including UINT32_MAX. */
    std::optional<int64_t> label;
};

/** A maximal sequence of instructions with a single entry and a single normal exit. */
struct BasicBlock
{
    uint32_t index;        ///< Position of this block in Cfg::blocks().
    uint32_t start_offset; ///< Code-unit offset of the first instruction.
    uint32_t end_offset;   ///< Code-unit offset one past the last instruction.
    uint32_t first_insn;   ///< Index into the method's decoded instruction span.
    uint32_t insn_count;   ///< Number of instructions covered by this block.
    std::vector<CfgEdge> successors;
    std::vector<uint32_t> predecessors; ///< Block indices only.
    bool is_handler_entry; ///< True iff at least one Exception edge targets this block.
};

/** Control-flow graph for a single method.
 *
 *  Blocks are stored in `blocks()` in increasing `start_offset` order, with the
 *  entry block at index 0.  Edges reference target blocks by index, not pointer,
 *  so the structure is trivially copyable / movable. */
class Cfg
{
  public:
    Cfg() = default;

    std::span<const BasicBlock> blocks() const { return blocks_; }
    const BasicBlock &entry() const { return blocks_[0]; }

    /** Locate the block whose `start_offset == code_unit_offset`.
     *  Returns nullptr if no block starts at that offset. */
    const BasicBlock *block_at_offset(uint32_t code_unit_offset) const;

    bool empty() const { return blocks_.empty(); }

  private:
    friend Cfg build_cfg(const raw::CodeItem &code, std::span<const Instruction> insns);
    explicit Cfg(std::vector<BasicBlock> blocks);

    std::vector<BasicBlock> blocks_;
    /** Sorted (start_offset, block_index) pairs for binary-search lookup. */
    std::vector<std::pair<uint32_t, uint32_t>> offset_index_;
};

/** Build the CFG for a method's bytecode.
 *  @param code   Raw code item — needed for try blocks and catch handler resolution.
 *  @param insns  Decoded instruction stream (typically `Method::instructions()`).
 *  @return       A Cfg with one block per maximal straight-line region. */
Cfg build_cfg(const raw::CodeItem &code, std::span<const Instruction> insns);

} // namespace dex
