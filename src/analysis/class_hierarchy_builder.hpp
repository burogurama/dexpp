#pragma once

// Private header: not part of the public API (src/dex.hpp does not include
// it).  The public way to get the class hierarchy is
// AnalysisContext::class_hierarchy().

#include "analysis/class_hierarchy.hpp"

namespace dex::detail {

struct AnalysisContextImpl;

/** Build the whole-program class hierarchy of @p impl.  AnalysisContextImpl's
 *  get_class_hierarchy() calls it once and caches the result. */
ClassHierarchy build_class_hierarchy(const AnalysisContextImpl &impl);

} // namespace dex::detail
