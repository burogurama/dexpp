#pragma once

// Private header of the C shim: the handle definitions and the helpers shared
// by dexpp_c*.cpp and dexpp_model.cpp.  Not installed, not part of the API.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "dex.hpp"
#include "dexpp.h"

// ===== handles ===============================================================
//
// Every handle holds C++ objects that share ownership of the analysis, so a
// handle keeps it alive on its own (as the C++ handles do).

struct DexppContext
{
    dex::AnalysisContext ctx;
};
struct DexppApk
{
    dex::apk::Apk apk;
};
struct DexppClass
{
    dex::Class cls;
};
struct DexppClassRef
{
    dex::ClassRef ref;
};
struct DexppMethod
{
    dex::Method method;
};
struct DexppField
{
    dex::Field field;
};
struct DexppCallGraph
{
    dex::AnalysisContext ctx; // keeps the graph's owner alive
    const dex::CallGraph *graph;
};
struct DexppClassHierarchy
{
    dex::AnalysisContext ctx;
    const dex::ClassHierarchy *hierarchy;
};
struct DexppXrefs
{
    dex::AnalysisContext ctx;
    const dex::Xrefs *xrefs;
};
struct DexppError
{
    DexppErrorCode code;
    std::string message;
    std::vector<std::pair<std::string, std::string>> problems; // (where, message)
};

namespace dexpp_c {

// ===== strings and arrays ====================================================

/** malloc'd, NUL-terminated copy of @p sv (the C side frees it). */
inline char *cstr(std::string_view sv)
{
    char *p = static_cast<char *>(std::malloc(sv.size() + 1));
    if (p == nullptr)
        throw std::bad_alloc();
    // An empty view may have a null data pointer, which memcpy must not get.
    if (!sv.empty())
        std::memcpy(p, sv.data(), sv.size());
    p[sv.size()] = '\0';
    return p;
}

/** A DexppText holding a copy of @p sv. */
inline DexppText text(std::string_view sv) { return DexppText{cstr(sv), sv.size()}; }

/** malloc'd array of @p n zero-filled T, or NULL when @p n is 0 (the API's
 *  rule for empty arrays).  The C side frees it with free(). */
template <typename T> T *alloc_array(std::size_t n)
{
    if (n == 0)
        return nullptr;
    void *p = std::calloc(n, sizeof(T));
    if (p == nullptr)
        throw std::bad_alloc();
    return static_cast<T *>(p);
}

/** A malloc'd array of string copies; NULL and 0 for an empty input. */
template <typename Range> char **string_array(const Range &items, size_t *count)
{
    std::size_t n = 0;
    for ([[maybe_unused]] const auto &item : items)
        ++n;
    *count = n;
    char **out = alloc_array<char *>(n);
    std::size_t i = 0;
    for (const auto &item : items)
        out[i++] = cstr(std::string_view(item));
    return out;
}

/** A malloc'd array of uint32 indices; NULL and 0 for an empty input. */
template <typename Range> uint32_t *index_array(const Range &items, size_t *count)
{
    std::size_t n = 0;
    for ([[maybe_unused]] const auto &item : items)
        ++n;
    *count = n;
    uint32_t *out = alloc_array<uint32_t>(n);
    std::size_t i = 0;
    for (const auto &item : items)
        out[i++] = static_cast<uint32_t>(item);
    return out;
}

/** An array of new C handles, one per C++ handle in @p items. */
template <typename Handle, typename Range> Handle **handle_array(const Range &items, size_t *count)
{
    *count = items.size();
    Handle **out = alloc_array<Handle *>(items.size());
    for (std::size_t i = 0; i < items.size(); ++i)
        out[i] = new Handle{items[i]};
    return out;
}

// ===== identities ============================================================

inline DexppMethodId method_id(const dex::MethodId &id)
{
    return DexppMethodId{cstr(id.class_descriptor), cstr(id.name), cstr(id.proto)};
}
inline DexppFieldId field_id(const dex::FieldId &id)
{
    return DexppFieldId{cstr(id.class_descriptor), cstr(id.name), cstr(id.type)};
}
inline void clear(DexppMethodId &id)
{
    std::free(id.class_descriptor);
    std::free(id.name);
    std::free(id.proto);
    id = DexppMethodId{};
}
inline void clear(DexppFieldId &id)
{
    std::free(id.class_descriptor);
    std::free(id.name);
    std::free(id.type);
    id = DexppFieldId{};
}
inline void clear(DexppText &t)
{
    std::free(t.data);
    t = DexppText{};
}

// ===== enums: explicit mappings, so a new C++ value fails to compile =========
//
// Each switch lists every C++ enumerator and has no default, so the compiler
// warns (an error under DEXPP_WERROR) when the C++ enum gains a value.  The
// return after the switch only satisfies compilers that cannot see that.

inline DexppInvokeKind invoke_kind(dex::InvokeKind k)
{
    switch (k) {
    case dex::InvokeKind::Virtual:
        return DEXPP_INVOKE_VIRTUAL;
    case dex::InvokeKind::Super:
        return DEXPP_INVOKE_SUPER;
    case dex::InvokeKind::Direct:
        return DEXPP_INVOKE_DIRECT;
    case dex::InvokeKind::Static:
        return DEXPP_INVOKE_STATIC;
    case dex::InvokeKind::Interface:
        return DEXPP_INVOKE_INTERFACE;
    case dex::InvokeKind::Polymorphic:
        return DEXPP_INVOKE_POLYMORPHIC;
    case dex::InvokeKind::Custom:
        return DEXPP_INVOKE_CUSTOM;
    }
    return DEXPP_INVOKE_VIRTUAL;
}

inline DexppEdgeOrigin edge_origin(dex::EdgeOrigin o)
{
    switch (o) {
    case dex::EdgeOrigin::Declared:
        return DEXPP_EDGE_DECLARED;
    case dex::EdgeOrigin::InheritedResolution:
        return DEXPP_EDGE_INHERITED_RESOLUTION;
    }
    return DEXPP_EDGE_DECLARED;
}

inline DexppCallNodeKind call_node_kind(dex::CallNodeKind k)
{
    switch (k) {
    case dex::CallNodeKind::Defined:
        return DEXPP_CALL_NODE_DEFINED;
    case dex::CallNodeKind::External:
        return DEXPP_CALL_NODE_EXTERNAL;
    case dex::CallNodeKind::CallSite:
        return DEXPP_CALL_NODE_CALL_SITE;
    }
    return DEXPP_CALL_NODE_EXTERNAL;
}

inline DexppCfgEdgeKind cfg_edge_kind(dex::CfgEdgeKind k)
{
    switch (k) {
    case dex::CfgEdgeKind::Fallthrough:
        return DEXPP_CFG_FALLTHROUGH;
    case dex::CfgEdgeKind::Goto:
        return DEXPP_CFG_GOTO;
    case dex::CfgEdgeKind::BranchTaken:
        return DEXPP_CFG_BRANCH_TAKEN;
    case dex::CfgEdgeKind::BranchNotTaken:
        return DEXPP_CFG_BRANCH_NOT_TAKEN;
    case dex::CfgEdgeKind::SwitchCase:
        return DEXPP_CFG_SWITCH_CASE;
    case dex::CfgEdgeKind::SwitchDefault:
        return DEXPP_CFG_SWITCH_DEFAULT;
    case dex::CfgEdgeKind::Exception:
        return DEXPP_CFG_EXCEPTION;
    case dex::CfgEdgeKind::ExceptionCatchAll:
        return DEXPP_CFG_EXCEPTION_CATCH_ALL;
    }
    return DEXPP_CFG_FALLTHROUGH;
}

inline DexppTypeUseKind type_use_kind(dex::TypeUseKind k)
{
    switch (k) {
    case dex::TypeUseKind::ConstClass:
        return DEXPP_TYPE_USE_CONST_CLASS;
    case dex::TypeUseKind::CheckCast:
        return DEXPP_TYPE_USE_CHECK_CAST;
    case dex::TypeUseKind::InstanceOf:
        return DEXPP_TYPE_USE_INSTANCE_OF;
    case dex::TypeUseKind::NewInstance:
        return DEXPP_TYPE_USE_NEW_INSTANCE;
    case dex::TypeUseKind::NewArray:
        return DEXPP_TYPE_USE_NEW_ARRAY;
    case dex::TypeUseKind::FilledNewArray:
        return DEXPP_TYPE_USE_FILLED_NEW_ARRAY;
    }
    return DEXPP_TYPE_USE_CONST_CLASS;
}

inline DexppOperandWidth operand_width(dex::OperandWidth w)
{
    switch (w) {
    case dex::OperandWidth::Single:
        return DEXPP_WIDTH_SINGLE;
    case dex::OperandWidth::Pair:
        return DEXPP_WIDTH_PAIR;
    }
    return DEXPP_WIDTH_SINGLE;
}

/** The integer stored in an enum member of a struct the caller filled.  C lets
 *  the caller store any value of the enum's integer type there (a kind from a
 *  newer header, or garbage), but in C++ reading a value outside the range of
 *  the enumerators through the enum type is undefined behaviour.  So read the
 *  member's bytes as its underlying integer type instead. */
template <typename E> std::underlying_type_t<E> raw_enum(const E &member)
{
    std::underlying_type_t<E> raw;
    std::memcpy(&raw, &member, sizeof raw);
    return raw;
}

/** The width a caller stored; any value but DEXPP_WIDTH_PAIR counts as single. */
inline dex::OperandWidth operand_width(const DexppOperandWidth &w)
{
    using Raw = std::underlying_type_t<DexppOperandWidth>;
    return raw_enum(w) == static_cast<Raw>(DEXPP_WIDTH_PAIR) ? dex::OperandWidth::Pair
                                                             : dex::OperandWidth::Single;
}

inline DexppAnnotationVisibility visibility(dex::AnnotationVisibility v)
{
    switch (v) {
    case dex::AnnotationVisibility::Build:
        return DEXPP_VISIBILITY_BUILD;
    case dex::AnnotationVisibility::Runtime:
        return DEXPP_VISIBILITY_RUNTIME;
    case dex::AnnotationVisibility::System:
        return DEXPP_VISIBILITY_SYSTEM;
    }
    return DEXPP_VISIBILITY_BUILD;
}

inline DexppComponentKind component_kind(dex::apk::Component::Kind k)
{
    switch (k) {
    case dex::apk::Component::Kind::Activity:
        return DEXPP_COMPONENT_ACTIVITY;
    case dex::apk::Component::Kind::Service:
        return DEXPP_COMPONENT_SERVICE;
    case dex::apk::Component::Kind::Receiver:
        return DEXPP_COMPONENT_RECEIVER;
    case dex::apk::Component::Kind::Provider:
        return DEXPP_COMPONENT_PROVIDER;
    }
    return DEXPP_COMPONENT_ACTIVITY;
}

// ===== errors ================================================================

inline DexppErrorCode error_code(dex::AnalysisError::Code c)
{
    switch (c) {
    case dex::AnalysisError::Code::FileNotFound:
        return DEXPP_ERROR_FILE_NOT_FOUND;
    case dex::AnalysisError::Code::CannotRead:
        return DEXPP_ERROR_CANNOT_READ;
    case dex::AnalysisError::Code::InvalidDexFile:
        return DEXPP_ERROR_INVALID_DEX;
    case dex::AnalysisError::Code::InvalidApk:
        return DEXPP_ERROR_INVALID_APK;
    }
    return DEXPP_ERROR_INTERNAL;
}

inline DexppErrorCode error_code(dex::model::Error::Kind k)
{
    switch (k) {
    case dex::model::Error::Kind::FileNotFound:
        return DEXPP_ERROR_FILE_NOT_FOUND;
    case dex::model::Error::Kind::CannotRead:
        return DEXPP_ERROR_CANNOT_READ;
    case dex::model::Error::Kind::CannotWrite:
        return DEXPP_ERROR_CANNOT_WRITE;
    case dex::model::Error::Kind::Malformed:
        return DEXPP_ERROR_INVALID_DEX;
    case dex::model::Error::Kind::Unsupported:
        return DEXPP_ERROR_UNSUPPORTED_DEX;
    case dex::model::Error::Kind::Invalid:
        return DEXPP_ERROR_INVALID_MODEL;
    case dex::model::Error::Kind::TooLarge:
        return DEXPP_ERROR_POOL_OVERFLOW;
    case dex::model::Error::Kind::VersionTooLow:
        return DEXPP_ERROR_VERSION_TOO_LOW;
    }
    return DEXPP_ERROR_INTERNAL;
}

/** Store a new error in *err, when the caller asked for one. */
inline void set_error(DexppError **err, DexppErrorCode code, std::string message)
{
    if (err == nullptr)
        return;
    *err = new (std::nothrow) DexppError{code, std::move(message), {}};
}

inline void set_error(DexppError **err, const dex::AnalysisError &e)
{
    set_error(err, error_code(e.code), e.message);
}

inline void set_error(DexppError **err, const dex::model::Error &e)
{
    if (err == nullptr)
        return;
    auto *error = new (std::nothrow) DexppError{error_code(e.kind), e.message, {}};
    if (error != nullptr)
        for (const auto &p : e.problems)
            error->problems.emplace_back(p.where, p.message);
    *err = error;
}

/** Clear *err at the start of a fallible call (the "success sets NULL" rule). */
inline void reset(DexppError **err)
{
    if (err != nullptr)
        *err = nullptr;
}

/** Run @p body for a fallible function: an exception (out of memory, a
 *  thread that cannot start) becomes an INTERNAL error instead of crossing
 *  into C, and the function returns @p failed. */
template <typename R, typename Body> R guarded(DexppError **err, R failed, Body &&body)
{
    try {
        return body();
    }
    catch (const std::exception &e) {
        set_error(err, DEXPP_ERROR_INTERNAL, e.what());
    }
    catch (...) {
        set_error(err, DEXPP_ERROR_INTERNAL, "unknown failure");
    }
    return failed;
}

// ===== values and annotations (dexpp_c_code.cpp) =============================

DexppValue value(const dex::AnnotationValue &v);
DexppAnnotation *annotation_array(const std::vector<dex::Annotation> &items, size_t *count);
void clear(DexppValue &v);

} // namespace dexpp_c
