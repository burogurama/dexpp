#include "cfg.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <type_traits>
#include <unordered_map>
#include <variant>

namespace dex {

Cfg::Cfg(std::vector<BasicBlock> blocks) : blocks_(std::move(blocks))
{
    offset_index_.reserve(blocks_.size());
    for (const auto &b : blocks_)
        offset_index_.emplace_back(b.start_offset, b.index);
    // build_cfg constructs blocks_ in increasing start_offset order, so
    // offset_index_ is already sorted.
}

const BasicBlock *Cfg::block_at_offset(uint32_t code_unit_offset) const
{
    auto it = std::lower_bound(
        offset_index_.begin(), offset_index_.end(), code_unit_offset,
        [](const std::pair<uint32_t, uint32_t> &p, uint32_t v) { return p.first < v; });
    if (it == offset_index_.end() || it->first != code_unit_offset)
        return nullptr;
    return &blocks_[it->second];
}

namespace {

constexpr uint32_t kCatchAllLabel = std::numeric_limits<uint32_t>::max();

// Sentinel-free leader collector: pushes only offsets that map to a real
// instruction in `off_to_idx`.  Mismatched leaders (e.g. branch target inside
// a multi-unit instruction in malformed DEX) are silently dropped.
struct LeaderSet
{
    std::vector<uint32_t> values;

    void try_insert(uint32_t off, const std::unordered_map<uint32_t, uint32_t> &off_to_idx)
    {
        if (off_to_idx.find(off) != off_to_idx.end())
            values.push_back(off);
    }

    void finalize()
    {
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
    }

    /** Returns the block index of @p off, or std::nullopt if @p off is not a leader. */
    std::optional<uint32_t> block_of(uint32_t off) const
    {
        auto it = std::lower_bound(values.begin(), values.end(), off);
        if (it == values.end() || *it != off)
            return std::nullopt;
        return static_cast<uint32_t>(std::distance(values.begin(), it));
    }
};

} // namespace

Cfg build_cfg(const raw::CodeItem &code, std::span<const Instruction> insns)
{
    if (insns.empty())
        return Cfg{};

    // A. offset -> instruction index
    std::unordered_map<uint32_t, uint32_t> off_to_idx;
    off_to_idx.reserve(insns.size());
    for (uint32_t i = 0; i < static_cast<uint32_t>(insns.size()); ++i)
        off_to_idx[base_of(insns[i]).offset] = i;

    // One past the last decoded instruction.  Trailing payload data (skipped
    // by decode_instructions) is intentionally excluded — payloads are not
    // executable and never appear as block ranges.
    uint32_t insns_end = base_of(insns.back()).offset + base_of(insns.back()).size;

    // B. Compute leaders
    LeaderSet leaders;
    leaders.try_insert(base_of(insns.front()).offset, off_to_idx);

    for (const auto &insn : insns) {
        const auto &b = base_of(insn);
        uint32_t next_off = b.offset + b.size;

        std::visit(
            [&](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, GotoInstruction>) {
                    leaders.try_insert(b.offset + v.branch_offset, off_to_idx);
                    // next_off must be a leader too: any dead/unreachable code
                    // after the goto starts a new block, so the goto stays at
                    // the end of its own block (where section D dispatches).
                    leaders.try_insert(next_off, off_to_idx);
                }
                else if constexpr (std::is_same_v<T, IfInstruction>) {
                    leaders.try_insert(b.offset + v.branch_offset, off_to_idx);
                    leaders.try_insert(next_off, off_to_idx);
                }
                else if constexpr (std::is_same_v<T, IfZeroInstruction>) {
                    leaders.try_insert(b.offset + v.branch_offset, off_to_idx);
                    leaders.try_insert(next_off, off_to_idx);
                }
                else if constexpr (std::is_same_v<T, SwitchInstruction>) {
                    leaders.try_insert(next_off, off_to_idx);
                    for (const auto &c : v.cases)
                        leaders.try_insert(b.offset + c.target, off_to_idx);
                }
                else if constexpr (std::is_same_v<T, ReturnInstruction> ||
                                   std::is_same_v<T, ThrowInstruction>) {
                    // Next instruction (if any) starts a new block; if none, no leader added.
                    leaders.try_insert(next_off, off_to_idx);
                }
            },
            insn);
    }

    // Try-block boundaries
    for (const auto &t : code.tries) {
        leaders.try_insert(t.start_addr, off_to_idx);
        leaders.try_insert(t.start_addr + t.insn_count, off_to_idx);
    }
    // Catch-handler entries
    for (const auto &h : code.handlers) {
        for (const auto &p : h.handlers)
            leaders.try_insert(p.addr, off_to_idx);
        if (h.catch_all_addr.has_value())
            leaders.try_insert(*h.catch_all_addr, off_to_idx);
    }

    leaders.finalize();
    if (leaders.values.empty())
        return Cfg{};

    // C. Build blocks
    std::vector<BasicBlock> blocks;
    blocks.reserve(leaders.values.size());
    for (std::size_t k = 0; k < leaders.values.size(); ++k) {
        BasicBlock blk{};
        blk.index = static_cast<uint32_t>(k);
        blk.start_offset = leaders.values[k];
        blk.end_offset = (k + 1 < leaders.values.size()) ? leaders.values[k + 1] : insns_end;
        blk.first_insn = off_to_idx[blk.start_offset];
        // Each leader is a real instruction offset (try_insert filters), and
        // the next leader (or insns_end) marks the next instruction boundary.
        // So insn_count is just the index delta — no inner scan needed.
        uint32_t next_first = (k + 1 < leaders.values.size()) ? off_to_idx[leaders.values[k + 1]]
                                                              : static_cast<uint32_t>(insns.size());
        blk.insn_count = next_first - blk.first_insn;
        blk.is_handler_entry = false;
        blocks.push_back(std::move(blk));
    }

    // D. Normal successors
    for (auto &blk : blocks) {
        const auto &last = insns[blk.first_insn + blk.insn_count - 1];
        const auto &lb = base_of(last);
        const uint32_t next = lb.offset + lb.size;

        std::visit(
            [&](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, GotoInstruction>) {
                    if (auto t = leaders.block_of(lb.offset + v.branch_offset))
                        blk.successors.push_back({*t, EdgeKind::Goto, std::nullopt});
                }
                else if constexpr (std::is_same_v<T, IfInstruction>) {
                    if (auto t = leaders.block_of(lb.offset + v.branch_offset))
                        blk.successors.push_back({*t, EdgeKind::BranchTaken, std::nullopt});
                    if (auto t = leaders.block_of(next))
                        blk.successors.push_back({*t, EdgeKind::BranchNotTaken, std::nullopt});
                }
                else if constexpr (std::is_same_v<T, IfZeroInstruction>) {
                    if (auto t = leaders.block_of(lb.offset + v.branch_offset))
                        blk.successors.push_back({*t, EdgeKind::BranchTaken, std::nullopt});
                    if (auto t = leaders.block_of(next))
                        blk.successors.push_back({*t, EdgeKind::BranchNotTaken, std::nullopt});
                }
                else if constexpr (std::is_same_v<T, SwitchInstruction>) {
                    for (const auto &c : v.cases) {
                        if (auto t = leaders.block_of(lb.offset + c.target))
                            blk.successors.push_back(
                                {*t, EdgeKind::SwitchCase, static_cast<int64_t>(c.key)});
                    }
                    if (auto t = leaders.block_of(next))
                        blk.successors.push_back({*t, EdgeKind::SwitchDefault, std::nullopt});
                }
                else if constexpr (std::is_same_v<T, ReturnInstruction> ||
                                   std::is_same_v<T, ThrowInstruction>) {
                    // No normal successors.
                }
                else {
                    // Non-terminator block ender → fallthrough.
                    if (auto t = leaders.block_of(next))
                        blk.successors.push_back({*t, EdgeKind::Fallthrough, std::nullopt});
                }
            },
            last);
    }

    // E. Exception edges (per-block).
    for (const auto &t : code.tries) {
        // Resolve handler_off → handlers index via handler_byte_offsets.
        auto hit = std::find(code.handler_byte_offsets.begin(), code.handler_byte_offsets.end(),
                             t.handler_off);
        if (hit == code.handler_byte_offsets.end())
            continue;
        std::size_t handler_idx =
            static_cast<std::size_t>(std::distance(code.handler_byte_offsets.begin(), hit));
        if (handler_idx >= code.handlers.size())
            continue;
        const auto &handler = code.handlers[handler_idx];

        uint32_t try_end = t.start_addr + t.insn_count;
        for (auto &blk : blocks) {
            if (blk.start_offset < t.start_addr)
                continue;
            if (blk.start_offset >= try_end)
                continue;

            for (const auto &p : handler.handlers) {
                if (auto target = leaders.block_of(p.addr)) {
                    blk.successors.push_back(
                        {*target, EdgeKind::Exception, static_cast<int64_t>(p.type_idx)});
                    blocks[*target].is_handler_entry = true;
                }
            }
            if (handler.catch_all_addr.has_value()) {
                if (auto target = leaders.block_of(*handler.catch_all_addr)) {
                    blk.successors.push_back(
                        {*target, EdgeKind::Exception, static_cast<int64_t>(kCatchAllLabel)});
                    blocks[*target].is_handler_entry = true;
                }
            }
        }
    }

    // F. Predecessors mirror successors, deduped.  Multiple parallel edges
    // (e.g. several switch cases targeting the same block) collapse to one
    // predecessor entry — most consumers expect predecessor uniqueness.
    for (const auto &blk : blocks) {
        for (const auto &edge : blk.successors)
            blocks[edge.target_block].predecessors.push_back(blk.index);
    }
    for (auto &blk : blocks) {
        std::sort(blk.predecessors.begin(), blk.predecessors.end());
        blk.predecessors.erase(std::unique(blk.predecessors.begin(), blk.predecessors.end()),
                               blk.predecessors.end());
    }

    return Cfg(std::move(blocks));
}

} // namespace dex
