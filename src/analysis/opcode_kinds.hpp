#pragma once

// Private header: not part of the public API (src/dex.hpp does not include
// it).  The public API gives the kinds already resolved: MethodCall::kind,
// TypeUse::kind, CallEdge::kind and the xref sites.

#include <cstdint>
#include <optional>

#include "analysis/call_graph.hpp" // InvokeKind
#include "analysis/xrefs.hpp"      // TypeUseKind

namespace dex::detail {

/** Map a Dalvik invoke opcode to its InvokeKind.  Returns std::nullopt for any
 *  opcode that is not a recognised invoke. */
std::optional<InvokeKind> invoke_kind_from_opcode(uint8_t opcode);

/** Map a type-referencing opcode to its TypeUseKind.  Returns std::nullopt for
 *  any opcode that does not carry a type operand (e.g. array-length). */
std::optional<TypeUseKind> type_use_kind_from_opcode(uint8_t opcode);

} // namespace dex::detail
