#pragma once

/** Parser for Android binary XML (AXML), the format of AndroidManifest.xml
 *  inside an APK.  This is a generic XML-tree decoder; the typed manifest view
 *  lives in apk/manifest.hpp. */

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/error.hpp"

namespace dex::axml {

/** A typed attribute value (Res_value).  `data` is interpreted per `type`. */
struct TypedValue
{
    enum class Type : uint8_t {
        Null = 0x00,
        Reference = 0x01, ///< data = resource id (0x7f....)
        Attribute = 0x02,
        String = 0x03, ///< data = string-pool index
        Float = 0x04,
        Dimension = 0x05,
        Fraction = 0x06,
        IntDec = 0x10,
        IntHex = 0x11,
        Bool = 0x12, ///< data: 0 = false, nonzero = true
        ColorArgb8 = 0x1c,
        ColorRgb8 = 0x1d,
        ColorArgb4 = 0x1e,
        ColorRgb4 = 0x1f,
    };

    uint8_t type = 0; ///< Raw dataType byte; compare against Type values.
    uint32_t data = 0;

    bool is(Type t) const { return type == static_cast<uint8_t>(t); }
};

/** One attribute of an element.  String-typed values are pre-resolved into
 *  `raw_value`; other types live in `typed`. */
struct XmlAttribute
{
    std::string namespace_uri; ///< e.g. "http://schemas.android.com/apk/res/android"; may be empty.
    std::string name;          ///< e.g. "exported".
    /** The text the attribute's raw-value index names, or, when there is
     *  none, the text of a string-typed value; else empty.  Android's
     *  XmlPullParser.getAttributeValue() reads this one. */
    std::string raw_value;
    TypedValue typed;
    /** When `typed` is a string (TYPE_STRING): the text its data indexes;
     *  else empty.  Android's TypedArray, which the package parser uses for
     *  android:name, android:exported and every other framework attribute,
     *  reads this one and ignores the raw value.  aapt and aapt2 put the same
     *  index in both, but a hand-made file may not. */
    std::string typed_string;
    /** The attribute's resource id, from the document's resource map: for
     *  example 0x01010010 for android:exported.  0 when the attribute has none
     *  (the attribute is not a framework attribute, or the document has no
     *  resource map).  Android identifies framework attributes by this id, not
     *  by `name`. */
    uint32_t resource_id = 0;

    /** The value as text, when it is one (TYPE_STRING or a raw value). */
    std::optional<std::string_view> as_string() const
    {
        if (!raw_value.empty() || typed.is(TypedValue::Type::String))
            return raw_value;
        return std::nullopt;
    }
    /** The value as an integer (TYPE_INT_DEC / TYPE_INT_HEX). */
    std::optional<int64_t> as_int() const
    {
        if (typed.is(TypedValue::Type::IntDec) || typed.is(TypedValue::Type::IntHex))
            return static_cast<int32_t>(typed.data);
        return std::nullopt;
    }
    /** The value as a boolean (TYPE_INT_BOOLEAN). */
    std::optional<bool> as_bool() const
    {
        if (typed.is(TypedValue::Type::Bool))
            return typed.data != 0;
        return std::nullopt;
    }
    /** The value as a resource reference (TYPE_REFERENCE), e.g. 0x7f0b0014. */
    std::optional<uint32_t> as_reference() const
    {
        if (typed.is(TypedValue::Type::Reference))
            return typed.data;
        return std::nullopt;
    }
};

/** One element of the decoded XML tree. */
struct XmlNode
{
    std::string name; ///< Element tag, e.g. "activity".
    std::vector<XmlAttribute> attributes;
    std::vector<XmlNode> children;
    std::string text; ///< Concatenated CDATA content, if any.

    /** First attribute with this local name (namespace and resource id
     *  ignored), or nullptr.  For a framework attribute such as
     *  android:exported, Android goes by the resource id instead; see
     *  attribute_by_id(). */
    const XmlAttribute *attribute(std::string_view attr_name) const
    {
        for (const auto &a : attributes)
            if (a.name == attr_name)
                return &a;
        return nullptr;
    }
    /** First attribute with this resource id, or nullptr.  An id of 0 matches
     *  nothing. */
    const XmlAttribute *attribute_by_id(uint32_t resource_id) const
    {
        if (resource_id == 0)
            return nullptr;
        for (const auto &a : attributes)
            if (a.resource_id == resource_id)
                return &a;
        return nullptr;
    }
    /** First child element with this tag, or nullptr. */
    const XmlNode *child(std::string_view tag) const
    {
        for (const auto &c : children)
            if (c.name == tag)
                return &c;
        return nullptr;
    }
    /** All child elements with this tag. */
    std::vector<const XmlNode *> children_named(std::string_view tag) const
    {
        std::vector<const XmlNode *> out;
        for (const auto &c : children)
            if (c.name == tag)
                out.push_back(&c);
        return out;
    }
};

struct XmlDocument
{
    XmlNode root;
    /** True when the document has a resource map, which gives attribute names
     *  their resource ids.  Every file aapt or aapt2 writes has one. */
    bool has_resource_map = false;
};

/** Decode a binary-XML buffer into a tree.  The whole tree is owned strings —
 *  no views into @p buf survive the call.  Fails with ErrorCode::InvalidXml
 *  when @p buf is not a binary XML document or is too damaged to read. */
std::expected<XmlDocument, Error> parse(std::span<const uint8_t> buf);

} // namespace dex::axml
