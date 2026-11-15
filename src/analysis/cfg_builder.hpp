#pragma once

// Private header: not part of the public API (src/dex.hpp does not include
// it).  The public way to get a control-flow graph is Method::cfg().

#include <span>

#include "analysis/cfg.hpp"
#include "analysis/instruction.hpp"
#include "raw/types.hpp"

namespace dex::detail {

/** Build the CFG for a method's bytecode.
 *  @param code   Raw code item — needed for try blocks and catch handler resolution.
 *  @param insns  Decoded instruction stream (typically `Method::instructions()`).
 *  @return       A Cfg with one block per maximal straight-line region. */
Cfg build_cfg(const raw::CodeItem &code, std::span<const Instruction> insns);

} // namespace dex::detail
