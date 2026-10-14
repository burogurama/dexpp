#pragma once

/** Internal header — not part of the public API (dex.hpp does not include it). */

#include <cstdint>
#include <span>
#include <vector>

namespace dex::detail {

/** One invoke instruction found by find_invokes(). */
struct InvokeSite
{
    uint32_t offset; ///< Code-unit offset of the instruction.
    uint8_t opcode;  ///< invoke-kind (0x6e–0x72), its /range form (0x74–0x78),
                     ///< invoke-polymorphic[/range] (0xfa–0xfb) or invoke-custom[/range]
                     ///< (0xfc–0xfd).
    uint16_t index;  ///< method_ids index, or call_site_ids index for invoke-custom.
};

/** Append the invoke instructions of @p insns to @p out, in order.  Walks the
 *  instruction boundaries exactly like decode_instructions(), so the result is
 *  the invokes that decoder reports — same offsets, same index (0 when the
 *  instruction is truncated) — without decoding any other instruction. */
void find_invokes(std::span<const uint16_t> insns, std::vector<InvokeSite> &out);

} // namespace dex::detail
