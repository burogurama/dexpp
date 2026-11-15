#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "access_flags.hpp"
#include "annotations.hpp"
#include "cfg.hpp"
#include "class_ref.hpp"
#include "instruction.hpp"
#include "xrefs.hpp" // MethodId, FieldId, InvokeKind, TypeUseKind

namespace dex {

namespace detail {
struct AnalysisContextImpl;
} // namespace detail

/** One outgoing call site in a method's body. */
struct MethodCall
{
    uint32_t code_offset; ///< Code-unit offset of the invoke instruction.
    MethodId target;      ///< Declared callee (no class-hierarchy resolution).
    InvokeKind kind;
};

/** One field read or write in a method's body. */
struct FieldAccess
{
    uint32_t code_offset;
    FieldId field;
    bool is_write;  ///< true for iput*/sput*.
    bool is_static; ///< true for sget*/sput*.
};

/** One const-string load in a method's body. */
struct StringLoad
{
    uint32_t code_offset;
    std::string_view value; ///< Decoded string; valid for the context's lifetime.
};

/** One type reference (const-class / check-cast / instance-of / new-instance /
 *  new-array / filled-new-array) in a method's body. */
struct TypeUse
{
    uint32_t code_offset;
    std::string_view descriptor; ///< Referenced type descriptor; context-lifetime.
    TypeUseKind kind;
};

/** One typed catch clause within a TryBlock. */
struct CatchHandler
{
    std::string_view type_descriptor; ///< Caught exception type ('L...;').
    uint32_t handler_offset;          ///< Code-unit offset of the handler block.
};

/** A try region and its handlers, decoded from the method's code item. */
struct TryBlock
{
    uint32_t start_offset;                    ///< First covered code-unit offset.
    uint32_t end_offset;                      ///< One past the last covered code unit.
    std::vector<CatchHandler> handlers;       ///< Typed catches, in DEX order.
    std::optional<uint32_t> catch_all_offset; ///< finally / catch-all handler, if any.
};

/** An incoming parameter and the register that holds it on method entry.
 *
 *  Dalvik places incoming arguments in the last `ins_size` registers of the
 *  frame.  This resolves that convention: the implicit `this` of an instance
 *  method comes first, then declared parameters in order, with wide (long /
 *  double) parameters occupying a register pair (`reg` and `reg + 1`). */
struct ParameterRegister
{
    uint16_t reg; ///< First register holding the parameter (low half if wide).
    /** Descriptor of the declared type, such as "I" or "Ljava/lang/String;";
     *  the declaring class for `this`.  Valid for the context's lifetime. */
    std::string_view type;
    bool is_this; ///< True for the implicit receiver of an instance method.
    bool is_wide; ///< True for long / double (occupies reg and reg + 1).
};

/** A lightweight, non-owning handle to a single DEX method.
 *
 *  Instances are normally obtained from Class::methods().  The handle stores the
 *  absolute method_ids index plus the access flags and code offset that come from
 *  the EncodedMethod entry in class_data_item (not from the MethodIdItem itself). */
class Method
{
  public:
    /** Used by the library; get handles from Class::methods() instead.
     *  @param impl            The context the handle belongs to and keeps alive.
     *  @param dex_idx         Index of the DEX file inside that context.
     *  @param method_abs_idx  Absolute index into the DEX file's method_ids table.
     *  @param access_flags    Flags from the enclosing EncodedMethod.
     *  @param code_off        Byte offset of the code_item, or 0 for abstract/native. */
    Method(std::shared_ptr<const detail::AnalysisContextImpl> impl, std::size_t dex_idx,
           uint32_t method_abs_idx, uint32_t access_flags, uint32_t code_off);

    /** Simple name of the method (e.g. "<init>", "main"). */
    std::string_view name() const;

    /** ClassRef for the class that defines this method. */
    ClassRef declaring_class() const;

    /** The method's identity: its class, name and prototype.  It is the
     *  identity the call graph and the cross-references use, so a Method can be
     *  looked up in them with it. */
    MethodId id() const;

    /** The prototype: the parameter types inside parentheses, then the return
     *  type, e.g. "(Ljava/lang/String;I)V".  It is the `proto` of id().  The
     *  view stays valid for the lifetime of the AnalysisContext. */
    std::string_view proto() const;

    /** Descriptor of the return type, from the prototype: "V" for void, "I",
     *  "Ljava/lang/String;", "[B" and so on.  The view stays valid for the
     *  lifetime of the AnalysisContext. */
    std::string_view return_type() const;

    /** Descriptors of the parameter types, in declaration order; empty for a
     *  method without parameters.  The views stay valid for the lifetime of
     *  the AnalysisContext. */
    std::vector<std::string_view> parameters() const;

    AccessFlags access_flags() const { return static_cast<AccessFlags>(access_flags_); }

    /** True iff this method has a bytecode body (i.e. is neither abstract nor
     *  native).  Equivalent to !instructions().empty() but cheaper. */
    bool has_code() const { return code_off_ != 0; }

    /** Returns true for `<init>` and `<clinit>` methods. */
    bool is_constructor() const;

    bool is_static() const { return has_flag(access_flags(), AccessFlags::Static); }
    bool is_public() const { return has_flag(access_flags(), AccessFlags::Public); }
    bool is_private() const { return has_flag(access_flags(), AccessFlags::Private); }
    bool is_abstract() const { return has_flag(access_flags(), AccessFlags::Abstract); }
    bool is_native() const { return has_flag(access_flags(), AccessFlags::Native); }

    /** Decoded bytecode for this method, as a view into a per-context cache.
     *  Repeated calls return the same span; the underlying storage stays alive
     *  for as long as this Method (and any other handle into the same context).
     *  Returns an empty span for abstract/native methods (code_off == 0). */
    std::span<const Instruction> instructions() const;

    /** Control-flow graph for this method's bytecode.  Cached per-context;
     *  repeated calls return the same reference.  Returns an empty Cfg for
     *  abstract/native methods. */
    const Cfg &cfg() const;

    /** Total size of the method's register frame (`registers_size`), or 0 for
     *  abstract / native methods (which have no code item). */
    uint16_t register_count() const;

    /** Incoming parameters mapped to their entry registers, in declaration
     *  order (with `this` first for instance methods).  Empty for abstract /
     *  native methods, where no register frame exists. */
    std::vector<ParameterRegister> parameter_registers() const;

    /** Forward references — what this method's body reaches out to.  Each walks
     *  the decoded instruction stream (cheap; instructions are cached) and
     *  resolves constant-pool indices to identities.  Empty for abstract /
     *  native methods.  This is the per-method "xref-to" complement of the
     *  whole-program reverse index in AnalysisContext::xrefs(). */
    std::vector<MethodCall> calls() const;
    std::vector<FieldAccess> field_accesses() const;
    std::vector<StringLoad> string_loads() const;
    std::vector<TypeUse> type_uses() const;

    /** Try regions and their handlers, in DEX order.  Empty when the method has
     *  no try blocks (or is abstract / native). */
    std::vector<TryBlock> try_blocks() const;

    /** Annotations attached to this method, resolved.  Parsed on demand. */
    std::vector<Annotation> annotations() const;

    /** Two handles are equal when they come from the same analysis (one
     *  AnalysisContext, or copies of it) and point at the same method of the
     *  same DEX file.  Handles from two contexts that loaded the same file
     *  are not equal.  std::hash<Method> is consistent with it. */
    bool operator==(const Method &) const = default;

  private:
    friend struct std::hash<Method>;

    std::shared_ptr<const detail::AnalysisContextImpl> impl_;
    std::size_t dex_idx_;
    uint32_t method_abs_idx_;
    uint32_t access_flags_;
    uint32_t code_off_;
};

} // namespace dex

namespace std {

/** Hash of a Method handle, consistent with Method::operator==, so handles can
 *  be keys of std::unordered_map and std::unordered_set. */
template <> struct hash<dex::Method>
{
    std::size_t operator()(const dex::Method &method) const noexcept;
};

} // namespace std
