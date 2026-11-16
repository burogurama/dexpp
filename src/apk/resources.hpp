#pragma once

/** Parser for resources.arsc, the compiled resource table inside an APK.
 *
 *  Scope: forward resolution of a resource id (0xPPTTEEEE) to its name and,
 *  for string, boolean and integer resources, its value — enough to resolve
 *  manifest references like android:label="@string/app_name" or
 *  android:exported="@bool/is_exported".
 *
 *  A resource can have a value in several configurations: one per language,
 *  screen density, Android version, and so on (the res/values-fr or
 *  res/values-v31 folders of the app's sources).  values() lists every one of
 *  them.  The resolve_* functions give one value: the default configuration's
 *  (res/values), or, when the resource has no default value, the first
 *  configuration's in the table. */

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "common/error.hpp"

namespace dex::apk {

namespace detail {
struct StringPool;
struct ResourceTableAccess;
} // namespace detail

/** The fully-qualified name of a resource: package:type/entry. */
struct ResourceName
{
    std::string package; ///< e.g. "com.example.app".
    std::string type;    ///< e.g. "string", "drawable".
    std::string entry;   ///< e.g. "app_name".

    bool operator==(const ResourceName &) const = default;
};

/** The value of a resource in one configuration, as the table stores it.  A
 *  value that refers to another resource is not followed. */
struct ResourceValue
{
    /** The configuration, written like the qualifiers of a res/ folder name:
     *  "" for the default configuration (res/values), "v31" for Android 12
     *  and newer (res/values-v31), "fr" for French, "b+sr+Latn-land-night-v8"
     *  for a combination.  The text follows aapt2's `dump resources`. */
    std::string configuration;
    /** The value's type, a Res_value data type from Android's
     *  ResourceTypes.h: for example 0x01 a reference to another resource,
     *  0x03 a string, 0x10 a decimal integer, 0x11 a hexadecimal integer,
     *  0x12 a boolean, 0x1c to 0x1f a color. */
    uint8_t type = 0;
    /** The raw 32-bit data: the id of the resource a reference names, the
     *  integer itself, 0 or not 0 for a boolean (aapt2 writes true as
     *  0xFFFFFFFF), or the index of a string in the table's string pool. */
    uint32_t data = 0;
    /** The text of a string value (type 0x03); nullopt for any other
     *  type. */
    std::optional<std::string> string;

    /** The value read as a boolean, the way Android's TypedArray.getBoolean
     *  reads it: an integer-typed value is true when it is not 0; a string is
     *  true when it is exactly "1", "true" or "TRUE", and false otherwise; a
     *  floating-point value, a dimension or a fraction is false.  nullopt for
     *  a reference (not followed here), a null value or an empty string, for
     *  which Android uses the attribute's default. */
    std::optional<bool> as_bool() const;

    /** The value read as an integer: any integer-typed value (an integer, a
     *  boolean, a color), as its signed 32-bit data.  nullopt for any other
     *  type. */
    std::optional<int64_t> as_int() const;
};

class ResourceTable
{
  public:
    /** Decode a resources.arsc image.  Fails with ErrorCode::InvalidResources
     *  when @p buf is not a resource table, or holds neither a package nor a
     *  string. */
    static std::expected<ResourceTable, Error> parse(std::span<const uint8_t> buf);

    /** Name of the resource with id @p id, or nullopt if not present. */
    std::optional<ResourceName> name_of(uint32_t id) const;

    /** Every value of resource @p id, one for each configuration the table
     *  gives it, in table order.  Empty when the id is not in the table.  A
     *  complex entry, such as a style, an array or a plural, has no single
     *  value and is left out.  A value that refers to another resource is
     *  returned as it is: call values() on its id to follow it. */
    std::vector<ResourceValue> values(uint32_t id) const;

    /** Value of a string-typed resource @p id, resolved through the global
     *  value pool.  nullopt if the id is absent or its value is not a string.
     *
     *  This and the other resolve_* functions follow a resource whose value
     *  is a reference to another resource (`<bool name="a">@bool/b</bool>`)
     *  to the end of the chain.  Like Android, they give up after 20 steps,
     *  which also stops a reference loop.  The value used is the default
     *  configuration's; see the comment at the top of this file. */
    std::optional<std::string_view> resolve_string(uint32_t id) const;

    /** Value of resource @p id as a boolean, read the way Android's
     *  TypedArray.getBoolean reads it (see ResourceValue::as_bool): a
     *  `<bool>` resource, any integer-typed value (nonzero is true), or a
     *  string ("1", "true" and "TRUE" are true, any other text is false).
     *  nullopt if the id is absent, or its value is null or an empty string,
     *  for which Android uses the attribute's default. */
    std::optional<bool> resolve_bool(uint32_t id) const;

    /** Value of resource @p id as an integer: an `<integer>` resource, or any
     *  other integer-typed value (a boolean, a color), read as its signed
     *  32-bit data, as Android's TypedArray.getInt reads it.  (aapt2 writes a
     *  true boolean as -1.)  nullopt if the id is absent or its value is of
     *  another type, such as a string. */
    std::optional<int64_t> resolve_int(uint32_t id) const;

    /** Reverse lookup: the id of @p type / @p entry (first package that
     *  defines it), or nullopt.  Linear in the table; intended for tooling. */
    std::optional<uint32_t> id_of(std::string_view type, std::string_view entry) const;

    bool empty() const { return entries_.empty(); }

  private:
    friend struct detail::ResourceTableAccess;

    /** One configuration's value of an entry. */
    struct Value
    {
        uint32_t config = 0;    ///< Index into configs_.
        uint8_t type = 0;       ///< Res_value dataType, e.g. 0x03 string, 0x12 boolean.
        bool has_value = false; ///< false for a complex (map) entry.
        uint32_t data = 0;
    };

    /** A resource entry and its value in every configuration. */
    struct Entry
    {
        std::string type;
        std::string entry;
        std::vector<Value> values; ///< One per configuration, in table order.
        /** The value the resolve_* functions use: the first default
         *  configuration value, or the first value when there is none. */
        std::size_t primary = 0;
        bool has_default = false;
    };

    /** The value of @p id that the resolve_* functions use, after following
     *  references to other resources; nullptr if there is none (an unknown
     *  id, a complex entry, or a chain of more than 20 references). */
    const Value *value_entry(uint32_t id) const;

    /** The ResTable_typeSpec flags of @p id: the configuration axes (the
     *  ACONFIGURATION bits, such as 0x0004 for the locale) in which the
     *  entry has more than one value.  0 when the table has none for it. */
    uint32_t spec_flags(uint32_t id) const;

    void parse_package(std::span<const uint8_t> buf, std::size_t pkg_pos, std::size_t pkg_end);
    void parse_type_spec_chunk(std::span<const uint8_t> buf, std::size_t pos, std::size_t end,
                               uint32_t package_id);
    void parse_type_chunk(std::span<const uint8_t> buf, std::size_t pos, std::size_t end,
                          uint32_t package_id, const detail::StringPool &type_pool,
                          const detail::StringPool &key_pool);
    void record_entry(std::span<const uint8_t> buf, std::size_t pos, std::size_t end,
                      uint32_t res_id, const std::string &type_name,
                      const detail::StringPool &key_pool, uint32_t config, bool is_default);
    uint32_t intern_config(std::string name);

    std::string package_name_;
    std::vector<std::string> value_pool_; ///< Global value string pool.
    std::unordered_map<uint32_t, Entry> entries_;
    std::unordered_map<std::string, uint32_t> by_name_; ///< "type/entry" -> id.
    /** The configuration names values refer to; configs_[0] is the default
     *  configuration, "". */
    std::vector<std::string> configs_{std::string()};
    std::unordered_map<std::string, uint32_t> config_index_{{std::string(), 0}};
    /** The typeSpec flags of each type, keyed by (package id << 8) | type
     *  id, indexed by entry. */
    std::unordered_map<uint32_t, std::vector<uint32_t>> spec_flags_;
};

} // namespace dex::apk
