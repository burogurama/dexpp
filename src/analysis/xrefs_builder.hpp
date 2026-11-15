#pragma once

// Private header: not part of the public API (src/dex.hpp does not include
// it).  The public way to get the cross-reference index is
// AnalysisContext::xrefs().

#include "analysis/xrefs.hpp"

namespace dex::detail {

struct AnalysisContextImpl;

/** Build the whole-program cross-reference index of @p impl.
 *  AnalysisContextImpl's get_xrefs() calls it once and caches the result. */
Xrefs build_xrefs(const AnalysisContextImpl &impl);

} // namespace dex::detail
