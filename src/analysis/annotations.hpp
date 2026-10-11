#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "xrefs.hpp" // MethodId, FieldId

namespace dex {

/** Who can see an annotation at runtime, per the DEX visibility byte. */
enum class AnnotationVisibility : uint8_t {
    Build = 0x00,   ///< Compile-time only (e.g. @Override); stripped semantics.
    Runtime = 0x01, ///< Visible to reflection (RetentionPolicy.RUNTIME).
    System = 0x02,  ///< dalvik.annotation.* metadata (signatures, defaults, ...).
};

struct Annotation;

/** A fully resolved annotation element value.
 *
 *  Pool indices from the raw encoded_value are resolved to owned identities:
 *  strings/descriptors are copied out, enum/field references become FieldId,
 *  method references become MethodId.  All integral primitives (byte, short,
 *  char, int, long) widen to int64_t and float widens to double — the source
 *  primitive width is not preserved at this layer.  Values are owned and
 *  independent of the AnalysisContext's lifetime. */
struct AnnotationValue
{
    /** Class literal, e.g. `target = String.class`. */
    struct TypeRef
    {
        std::string descriptor;
        bool operator==(const TypeRef &) const = default;
    };
    /** Enum constant, identified by its static field. */
    struct EnumRef
    {
        FieldId constant;
        bool operator==(const EnumRef &) const = default;
    };
    /** Field reference (VALUE_FIELD; rare outside system annotations). */
    struct FieldRef
    {
        FieldId field;
        bool operator==(const FieldRef &) const = default;
    };
    /** Method reference (VALUE_METHOD; rare outside system annotations). */
    struct MethodRef
    {
        MethodId method;
        bool operator==(const MethodRef &) const = default;
    };
    /** Method prototype (VALUE_METHOD_TYPE), as a full "(...)ret" descriptor. */
    struct MethodTypeRef
    {
        std::string proto;
        bool operator==(const MethodTypeRef &) const = default;
    };
    /** Method handle (VALUE_METHOD_HANDLE); kept as the raw pool index. */
    struct MethodHandleRef
    {
        uint32_t index;
        bool operator==(const MethodHandleRef &) const = default;
    };

    std::variant<std::nullptr_t,        ///< null literal
                 bool, int64_t, double, ///< widened primitives
                 std::string,           ///< string literal
                 TypeRef, EnumRef, FieldRef, MethodRef, MethodTypeRef, MethodHandleRef,
                 std::vector<AnnotationValue>,     ///< array element
                 std::shared_ptr<const Annotation> ///< nested annotation (boxed)
                 >
        value;
};

/** One `name = value` pair of an annotation. */
struct AnnotationElement
{
    std::string name;
    AnnotationValue value;
};

/** A resolved annotation instance attached to a class, method, or field.
 *
 *  Only explicitly written elements appear in `elements` — values left at
 *  their @interface defaults are absent (DEX stores defaults separately, in
 *  the dalvik.annotation.AnnotationDefault system annotation on the
 *  annotation class itself). */
struct Annotation
{
    std::string type_descriptor; ///< Annotation class, 'L...;' form.
    AnnotationVisibility visibility;
    std::vector<AnnotationElement> elements; ///< In DEX (name-ascending) order.

    /** Value of the element named @p name, or nullptr if not explicitly set. */
    const AnnotationValue *find(std::string_view name) const
    {
        for (const auto &e : elements)
            if (e.name == name)
                return &e.value;
        return nullptr;
    }
};

} // namespace dex
