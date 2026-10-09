#pragma once

/** dex analysis public API.
 *
 *  Include this single header to gain access to the entire dex namespace:
 *  AnalysisContext, Class, Method, Field, ClassRef, TypeDescriptor,
 *  AccessFlags, AnalysisError, Cfg, CallGraph, ClassHierarchy, and Xrefs. */

#include "analysis/access_flags.hpp"
#include "analysis/analysis_context.hpp"
#include "analysis/analysis_error.hpp"
#include "analysis/annotations.hpp"
#include "analysis/call_graph.hpp"
#include "analysis/cfg.hpp"
#include "analysis/class.hpp"
#include "analysis/class_fwd.hpp"
#include "analysis/class_hierarchy.hpp"
#include "analysis/class_ref.hpp"
#include "analysis/field.hpp"
#include "analysis/instruction.hpp"
#include "analysis/method.hpp"
#include "analysis/type_descriptor.hpp"
#include "analysis/xrefs.hpp"
#include "apk/apk.hpp"
#include "apk/axml.hpp"
#include "apk/manifest.hpp"
#include "apk/resources.hpp"
#include "apk/signing.hpp"
