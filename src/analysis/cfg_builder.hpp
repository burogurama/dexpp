#pragma once

// Private header: not part of the public API (src/dex.hpp does not include
// it).  The public way to get a control-flow graph is Method::cfg().

#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

#include "analysis/cfg.hpp"
#include "analysis/instruction.hpp"
#include "raw/types.hpp"

namespace dex::detail {

/** Gives the descriptor of a DEX file's type index, for the catch types of
 *  exception edges.  The view must stay valid as long as the Cfg is used:
 *  the context passes AnalysisContextImpl::type_descriptor_of, whose views
 *  live as long as the context, which also owns the cached Cfg. */
using TypeResolver = std::function<std::string_view(uint32_t type_idx)>;

/** Build the CFG for a method's bytecode.
 *  @param code       Raw code item — needed for try blocks and catch handler resolution.
 *  @param insns      Decoded instruction stream (typically `Method::instructions()`).
 *  @param type_name  Resolves the type index of each typed catch handler to the
 *                    descriptor that CfgEdge::catch_type holds.
 *  @return           A Cfg with one block per maximal straight-line region. */
Cfg build_cfg(const raw::CodeItem &code, std::span<const Instruction> insns,
              const TypeResolver &type_name);

} // namespace dex::detail
