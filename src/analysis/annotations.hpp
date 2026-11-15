#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "common/annotation_visibility.hpp"
#include "common/method_handle_kind.hpp"
#include "xrefs.hpp" // MethodId, FieldId

namespace dex {

struct Annotation;

/** A fully resolved annotation element value.
 *
 *  Pool indices from the raw encoded_value are resolved to owned identities:
 *  strings/descriptors are copied out, enum/field references become FieldId,
 *  method references become MethodId.  All integral primitives (byte, short,
 *  char, int, long) widen to int64_t and float widens to double — the source
 *  primitive width is not preserved at this layer.  Values are owned and
 *  independent of the AnalysisContext's lifetime.
 *
 *  The nested types are plain values, named after the DEX encoded_value kind
 *  they hold.  They are not handles: none of them refers to the context. */
struct AnnotationValue
{
    /** Class literal, e.g. `target = String.class` (VALUE_TYPE). */
    struct TypeValue
    {
        std::string descriptor;
        bool operator==(const TypeValue &) const = default;
    };
    /** Enum constant, identified by its static field (VALUE_ENUM). */
    struct EnumValue
    {
        FieldId constant;
        bool operator==(const EnumValue &) const = default;
    };
    /** A field as a value (VALUE_FIELD; rare outside system annotations). */
    struct FieldValue
    {
        FieldId field;
        bool operator==(const FieldValue &) const = default;
    };
    /** A method as a value (VALUE_METHOD; rare outside system annotations). */
    struct MethodValue
    {
        MethodId method;
        bool operator==(const MethodValue &) const = default;
    };
    /** Method prototype (VALUE_METHOD_TYPE), as a full "(...)ret" descriptor. */
    struct MethodTypeValue
    {
        std::string proto;
        bool operator==(const MethodTypeValue &) const = default;
    };
    /** Method handle (VALUE_METHOD_HANDLE): what it does and the member it
     *  does it to.  The four field kinds hold a FieldId, the five invoke kinds
     *  a MethodId.  In a damaged file whose handle index is past the method
     *  handle table, or whose handle type is not one of the nine kinds, the
     *  kind is InvokeStatic and the member an empty MethodId (the "malformed
     *  pool index gives an empty identity" rule). */
    struct MethodHandleValue
    {
        MethodHandleKind kind;
        std::variant<MethodId, FieldId> member;
        bool operator==(const MethodHandleValue &) const = default;
    };

    std::variant<std::nullptr_t,        ///< null literal
                 bool, int64_t, double, ///< widened primitives
                 std::string,           ///< string literal
                 TypeValue, EnumValue, FieldValue, MethodValue, MethodTypeValue, MethodHandleValue,
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
 *  their `@interface` defaults are absent (DEX stores defaults separately, in
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
