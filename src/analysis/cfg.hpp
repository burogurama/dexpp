#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "instruction.hpp"

namespace dex {

namespace detail {
struct CfgAccess;
} // namespace detail

/** Kind of edge between two basic blocks. */
enum class CfgEdgeKind : uint8_t {
    Fallthrough,    ///< Straight-line continuation; only when the predecessor's last instruction
                    ///< is not a terminator.
    Goto,           ///< Unconditional goto / goto/16 / goto/32.
    BranchTaken,    ///< if-* / if-*z taken edge.
    BranchNotTaken, ///< if-* / if-*z fall-through edge.
    SwitchCase,     ///< Outgoing edge for one packed/sparse case; CfgEdge::case_key is its key.
    SwitchDefault,  ///< Outgoing edge for switch fall-through (the "default" case).
    Exception, ///< try-region → handler of one exception class; CfgEdge::catch_type names it.
    ExceptionCatchAll, ///< try-region → handler that catches every exception (a catch-all, as
                       ///< Java compiles `finally`).
};

/** A single outgoing edge from one basic block to another. */
struct CfgEdge
{
    uint32_t target_block;
    CfgEdgeKind kind;
    /** The case key.  Set for SwitchCase edges only. */
    std::optional<int32_t> case_key;
    /** Descriptor of the caught exception class, e.g. "Ljava/io/IOException;".
     *  Set for Exception edges only; an ExceptionCatchAll edge catches
     *  everything and has none.  The view points into the AnalysisContext's
     *  string cache and stays valid for the context's lifetime.  It is empty
     *  when the DEX file names a type index past its type table. */
    std::optional<std::string_view> catch_type;
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
    bool is_handler_entry; ///< True iff at least one Exception or ExceptionCatchAll edge
                           ///< targets this block.
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
    friend struct detail::CfgAccess; // the builder, in cfg.cpp
    explicit Cfg(std::vector<BasicBlock> blocks);

    std::vector<BasicBlock> blocks_;
    /** Sorted (start_offset, block_index) pairs for binary-search lookup. */
    std::vector<std::pair<uint32_t, uint32_t>> offset_index_;
};

} // namespace dex
