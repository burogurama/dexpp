#pragma once

/** Private to the APK layer: how Android reads a typed value as a boolean,
 *  and the values a resource can end at in every configuration.  Shared by
 *  resources.cpp and manifest.cpp; not part of the public API. */

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "apk/resources.hpp"

namespace dex::apk::detail {

// Res_value data types (ResourceTypes.h).
constexpr uint8_t kTypeNull = 0x00;
constexpr uint8_t kTypeReference = 0x01;
constexpr uint8_t kTypeAttribute = 0x02;
constexpr uint8_t kTypeString = 0x03;
constexpr uint8_t kTypeFloat = 0x04;
constexpr uint8_t kTypeDimension = 0x05;
constexpr uint8_t kTypeFraction = 0x06;
constexpr uint8_t kTypeDynamicReference = 0x07;
constexpr uint8_t kTypeDynamicAttribute = 0x08;
constexpr uint8_t kTypeFirstInt = 0x10;
constexpr uint8_t kTypeLastInt = 0x1f;

inline bool is_int_type(uint8_t type) { return type >= kTypeFirstInt && type <= kTypeLastInt; }

/** A reference to another resource, which the resource table can follow. */
inline bool is_resource_reference(uint8_t type)
{
    return type == kTypeReference || type == kTypeDynamicReference;
}

/** `@null`: a reference to id 0.  aapt2 writes `@null` this way, both in a
 *  binary XML attribute and in a resource value.  Android's attribute
 *  resolution (AttributeResolution.cpp) turns it into TYPE_NULL with
 *  DATA_NULL_UNDEFINED, the value of an attribute that is not written. */
inline bool is_null_reference(uint8_t type, uint32_t data)
{
    return is_resource_reference(type) && data == 0;
}

/** A value Android reads as "not written": TYPE_NULL, whatever its data
 *  (DATA_NULL_UNDEFINED, or DATA_NULL_EMPTY for `@empty`), or `@null`.
 *  TypedArray.hasValue() is false for both null forms, and getBoolean(),
 *  getInt(), getNonConfigurationString() and peekValue() give their default
 *  or null for both; only hasValueOrEmpty() tells `@empty` apart, and the
 *  package parser does not use it for the attributes dexpp reads. */
inline bool is_null_value(uint8_t type, uint32_t data)
{
    return type == kTypeNull || is_null_reference(type, data);
}

/** How Android reads a value as a boolean.
 *
 *  TypedArray.getBoolean(index, default) (frameworks/base,
 *  core/java/android/content/res/TypedArray.java) returns `data != 0` for an
 *  integer-typed value (TYPE_FIRST_INT to TYPE_LAST_INT, booleans included).
 *  For any other type it turns the value into text with
 *  TypedValue.coerceToString() and passes the text to
 *  XmlUtils.convertValueToBoolean(text, default)
 *  (core/java/com/android/internal/util/XmlUtils.java), which returns the
 *  default for null or empty text, true for exactly "1", "true" or "TRUE",
 *  and false for anything else.  So "True", "yes" and " true" are false.  A
 *  float, a dimension or a fraction becomes text such as "1.0" or "1.0dip",
 *  which is never one of the three, so it is false.
 *
 *  @p text is the string of a string value.  Returns nullopt where Android
 *  uses the caller's default: a null value, an empty string, and a type
 *  coerceToString() gives no text for.  Also nullopt for a reference or a
 *  theme attribute, which the caller resolves first. */
std::optional<bool> boolean_value(uint8_t type, uint32_t data,
                                  std::optional<std::string_view> text);

/** The typeSpec flags that make Android ignore a string read with
 *  TypedArray.getNonConfigurationString(index, 0), which is how the package
 *  parser reads android:name, android:permission and android:versionName.
 *  That function returns null when the value's "changing configurations",
 *  ActivityInfo.activityInfoConfigNativeToJava(flags), are not 0.  The
 *  conversion keeps only the native bits listed in
 *  ActivityInfo.CONFIG_NATIVE_BITS: every ACONFIGURATION bit from 0x0001
 *  (mcc) to 0x20000 (grammatical gender) except 0x0400 (the SDK version) and
 *  0x8000 (round screens).  So a string with a res/values-fr variant is
 *  ignored, while one with only a res/values-v31 variant is read, with the
 *  value of the device's Android version. */
constexpr uint32_t kNonConfigurationMask = 0x37BFF;

/** One value a resource can end at: not a reference.  A chain that ends at
 *  `@null` ends at a TYPE_NULL value (see is_null_value()). */
struct FinalValue
{
    uint8_t type = 0;
    uint32_t data = 0;

    bool operator==(const FinalValue &) const = default;
};

/** Every value a resource can end at, over every configuration. */
struct EveryValue
{
    /** The distinct values, references followed in every configuration. */
    std::vector<FinalValue> values;
    /** True when some configuration ends where no value can be read: an id
     *  the table does not have, a complex entry, a chain longer than Android
     *  follows, a loop, or more values than the search looks at. */
    bool incomplete = false;
    /** The typeSpec flags of every entry on the way, combined. */
    uint32_t spec_flags = 0;
    /** How many values the search looked at. */
    std::size_t visited = 0;
};

/** The private parts of ResourceTable that the manifest view uses. */
struct ResourceTableAccess
{
    /** Every value resource @p id ends at, in any configuration.  The
     *  search follows references breadth-first, in every configuration of
     *  every resource on the way, up to the 20 steps Android follows (so a
     *  loop is incomplete).  It looks at no more than @p limit values, so a
     *  hostile table cannot make it slow; past that it stops and reports
     *  `incomplete`. */
    static EveryValue every_value(const ResourceTable &table, uint32_t id, std::size_t limit);

    /** The value of @p id in the configuration the resolve_* functions use,
     *  references followed; a TYPE_NULL value when the chain reaches
     *  `@null`; nullopt when it cannot be read. */
    static std::optional<FinalValue> default_value(const ResourceTable &table, uint32_t id);

    /** String @p index of the table's value pool; nullopt when out of
     *  range. */
    static std::optional<std::string_view> pool_string(const ResourceTable &table, uint32_t index);
};

} // namespace dex::apk::detail
