#pragma once

/** Internal header — not part of the public API (dex.hpp does not include it).
 *
 *  Scanners that find some instructions of a method's code without decoding
 *  the others.  The whole-program builders use them, so building the call graph
 *  or the cross-references does not fill the per-context instruction cache.
 *  Both walk the instruction boundaries exactly like decode_instructions(), so
 *  they report the same instructions, at the same offsets, with the same pool
 *  indices as Method::instructions(). */

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

/** One instruction found by find_refs(). */
struct RefSite
{
    uint32_t offset; ///< Code-unit offset of the instruction.
    /** The pool index the instruction names: a string_ids index for
     *  const-string[/jumbo], a type_ids index for const-class, check-cast,
     *  instance-of, new-instance, new-array and filled-new-array[/range], a
     *  field_ids index for the iget/iput/sget/sput family, and a method_ids
     *  index for invoke-kind[/range] and invoke-polymorphic[/range]. */
    uint32_t index;
    uint8_t opcode; ///< The raw opcode byte; it tells which pool `index` is in.
};

/** True for the opcodes find_refs() reports: the instructions the
 *  cross-reference index (Xrefs) records. */
constexpr bool is_ref_opcode(uint8_t op)
{
    return op == 0x1a || op == 0x1b || op == 0x1c || op == 0x1f || op == 0x20 ||
           (op >= 0x22 && op <= 0x25) || (op >= 0x52 && op <= 0x72) || (op >= 0x74 && op <= 0x78) ||
           op == 0xfa || op == 0xfb;
}

/** Append the instructions of @p insns that name a string, type, field or
 *  method (see RefSite, is_ref_opcode) to @p out, in order.  Same boundary walk
 *  and same index as decode_instructions(): a truncated instruction reads its
 *  missing code units as 0.  invoke-custom, const-method-handle and
 *  const-method-type are not reported (Xrefs does not index them). */
void find_refs(std::span<const uint16_t> insns, std::vector<RefSite> &out);

} // namespace dex::detail
