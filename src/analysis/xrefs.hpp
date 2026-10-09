#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "call_graph.hpp" // MethodId, MethodIdHash, InvokeKind

namespace dex {

class Method;
namespace detail {
struct AnalysisContextImpl;
} // namespace detail

/** Canonical, DEX-portable identity for a field reference.
 *
 *  The triple (declaring class descriptor, name, type descriptor) uniquely
 *  identifies a field across all loaded DEX files — two fields may share a
 *  name and differ only in type.  Owned strings; safe to store and compare. */
struct FieldId
{
    std::string class_descriptor; ///< 'L...;' form, e.g. "Ljava/lang/System;"
    std::string name;             ///< Field name, e.g. "out"
    std::string type;             ///< Field type descriptor, e.g. "Ljava/io/PrintStream;"

    bool operator==(const FieldId &) const = default;
};

/** Hash functor for FieldId; suitable for std::unordered_map / std::unordered_set. */
struct FieldIdHash
{
    std::size_t operator()(const FieldId &f) const noexcept;
};

/** What a type-referencing instruction does with its type operand. */
enum class TypeRefKind : uint8_t {
    ConstClass,     ///< const-class                          (0x1c)
    CheckCast,      ///< check-cast                           (0x1f)
    InstanceOf,     ///< instance-of                          (0x20)
    NewInstance,    ///< new-instance                         (0x22)
    NewArray,       ///< new-array — descriptor is the *array* type, e.g. "[I"  (0x23)
    FilledNewArray, ///< filled-new-array / filled-new-array/range  (0x24, 0x25)
};

/** Map a type-referencing opcode to its TypeRefKind.  Returns std::nullopt for
 *  any opcode that does not carry a type operand (e.g. array-length). */
std::optional<TypeRefKind> type_ref_kind_from_opcode(uint8_t opcode);

/** One string-loading instruction (const-string / const-string/jumbo). */
struct StringXref
{
    uint32_t referrer;    ///< Index into Xrefs::referrers().
    uint32_t code_offset; ///< Code-unit offset of the instruction in the referrer.
};

/** One field-access instruction (iget* / iput* / sget* / sput*). */
struct FieldXref
{
    uint32_t referrer;
    uint32_t code_offset;
    bool is_write;  ///< true for iput*/sput*.
    bool is_static; ///< true for sget*/sput*.
};

/** One type-referencing instruction (see TypeRefKind). */
struct TypeXref
{
    uint32_t referrer;
    uint32_t code_offset;
    TypeRefKind kind;
};

/** One invoke instruction referencing a method (by its declared target). */
struct MethodXref
{
    uint32_t referrer;
    uint32_t code_offset;
    InvokeKind kind;
};

/** Whole-program cross-reference index for an AnalysisContext.
 *
 *  Answers "who references X" for methods (call sites), fields (reads/writes),
 *  strings (const-string loads), and types (const-class / new-instance /
 *  check-cast / instance-of / new-array / filled-new-array).  Built lazily on
 *  first `AnalysisContext::xrefs()` access and cached on the underlying impl.
 *
 *  Every site records the *referrer* — the defined method whose bytecode
 *  contains the instruction — as an index into `referrers()`, plus the
 *  instruction's code-unit offset.  `referrer_method()` materializes a Method
 *  handle on demand.
 *
 *  Method references are literal: each invoke site is indexed under its
 *  *declared* target MethodId, with no class-hierarchy fan-out (use
 *  `AnalysisContext::call_graph()` for dispatch-aware edges).  invoke-custom
 *  call sites and const-method-handle/-type operands are not indexed.
 *
 *  All spans and string_views returned by queries point into the index's own
 *  storage and stay valid for the lifetime of the AnalysisContext.
 *
 *  Move-only: copying would deep-copy every per-key site vector. */
class Xrefs
{
  public:
    Xrefs() = default;
    Xrefs(const Xrefs &) = delete;
    Xrefs &operator=(const Xrefs &) = delete;
    Xrefs(Xrefs &&) = default;
    Xrefs &operator=(Xrefs &&) = default;

    /** Identities of every defined method that contains at least one indexed
     *  instruction, in discovery order.  Site `referrer` fields index here. */
    std::span<const MethodId> referrers() const { return referrer_ids_; }

    /** Identity of one referrer.  Precondition: `referrer < referrers().size()`. */
    const MethodId &referrer_id(uint32_t referrer) const { return referrer_ids_[referrer]; }

    /** Materialize a Method handle for one referrer.  Returns nullopt if the
     *  owning AnalysisContext has been destroyed. */
    std::optional<Method> referrer_method(uint32_t referrer) const;

    /** All invoke sites whose declared target is exactly @p id. */
    std::span<const MethodXref> method_refs(const MethodId &id) const;

    /** All invoke sites whose declared target has the bare name @p name,
     *  in any class and with any prototype — e.g. every call to "exec" or
     *  "loadUrl".  Each result pairs the matched target with its sites. */
    std::vector<std::pair<const MethodId *, std::span<const MethodXref>>>
    method_refs_by_name(std::string_view name) const;

    /** All accesses (reads and writes) of the field @p id. */
    std::span<const FieldXref> field_refs(const FieldId &id) const;

    /** Only the reads (iget* / sget*) of @p id. */
    std::vector<FieldXref> field_reads(const FieldId &id) const;

    /** Only the writes (iput* / sput*) of @p id. */
    std::vector<FieldXref> field_writes(const FieldId &id) const;

    /** All const-string loads of exactly @p value. */
    std::span<const StringXref> string_refs(std::string_view value) const;

    /** Every distinct string value loaded somewhere in the program.  Useful as
     *  the enumeration step before substring/pattern filtering. */
    std::vector<std::string_view> referenced_strings() const;

    /** All type-referencing instructions naming @p descriptor. */
    std::span<const TypeXref> type_refs(std::string_view descriptor) const;

    bool empty() const
    {
        return method_map_.empty() && field_map_.empty() && string_map_.empty() &&
               type_map_.empty();
    }

  private:
    friend Xrefs build_xrefs(std::shared_ptr<const detail::AnalysisContextImpl>);

    /** Heterogeneous hasher/equal so queries take string_view without an
     *  intermediate std::string copy. */
    struct SvHash
    {
        using is_transparent = void;
        std::size_t operator()(std::string_view s) const noexcept
        {
            return std::hash<std::string_view>{}(s);
        }
        std::size_t operator()(const std::string &s) const noexcept
        {
            return std::hash<std::string_view>{}(s);
        }
    };
    struct SvEq
    {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
    };

    /** Back-reference to the defined method behind a referrer, used by
     *  referrer_method() to construct a Method handle on demand. */
    struct ReferrerRef
    {
        std::size_t dex_idx;
        uint32_t method_abs_idx;
        uint32_t access_flags;
        uint32_t code_off;
    };

    using MethodMap = std::unordered_map<MethodId, std::vector<MethodXref>, MethodIdHash>;

    std::vector<MethodId> referrer_ids_;
    std::vector<ReferrerRef> referrer_refs_; ///< parallel to referrer_ids_
    MethodMap method_map_;
    /** Bare method name → entries of method_map_.  Keys view the map's own
     *  MethodId keys; node-based maps keep them stable across moves. */
    std::unordered_map<std::string_view, std::vector<const MethodMap::value_type *>> name_index_;
    std::unordered_map<FieldId, std::vector<FieldXref>, FieldIdHash> field_map_;
    std::unordered_map<std::string, std::vector<StringXref>, SvHash, SvEq> string_map_;
    std::unordered_map<std::string, std::vector<TypeXref>, SvHash, SvEq> type_map_;
    std::weak_ptr<const detail::AnalysisContextImpl> impl_;
};

/** Build the whole-program cross-reference index from an analysis-context
 *  implementation.  Used internally by AnalysisContext::xrefs()'s lazy cache. */
Xrefs build_xrefs(std::shared_ptr<const detail::AnalysisContextImpl> impl);

} // namespace dex
