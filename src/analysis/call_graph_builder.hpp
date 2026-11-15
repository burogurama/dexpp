#pragma once

// Private header: not part of the public API (src/dex.hpp does not include
// it).  The public way to get the call graph is AnalysisContext::call_graph().

#include "analysis/call_graph.hpp"

namespace dex::detail {

struct AnalysisContextImpl;

/** Build the whole-program call graph of @p impl.  AnalysisContextImpl's
 *  get_call_graph() calls it once and caches the result. */
CallGraph build_call_graph(const AnalysisContextImpl &impl);

} // namespace dex::detail
