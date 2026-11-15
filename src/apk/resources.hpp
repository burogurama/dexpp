#pragma once

/** Parser for resources.arsc, the compiled resource table inside an APK.
 *
 *  Scope: forward resolution of a resource id (0xPPTTEEEE) to its name and,
 *  for string, boolean and integer resources, its value — enough to resolve
 *  manifest references like android:label="@string/app_name" or
 *  android:exported="@bool/is_exported".  Configuration variants (locale,
 *  density, API level) are collapsed to a single value, preferring the default
 *  (config-less) entry. */

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
} // namespace detail

/** The fully-qualified name of a resource: package:type/entry. */
struct ResourceName
{
    std::string package; ///< e.g. "com.example.app".
    std::string type;    ///< e.g. "string", "drawable".
    std::string entry;   ///< e.g. "app_name".

    bool operator==(const ResourceName &) const = default;
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

    /** Value of a string-typed resource @p id, resolved through the global
     *  value pool.  nullopt if the id is absent or its value is not a string.
     *
     *  This and the other resolve_* functions follow a resource whose value
     *  is a reference to another resource (`<bool name="a">@bool/b</bool>`)
     *  to the end of the chain.  Like Android, they give up after 20 steps,
     *  which also stops a reference loop.  The value used is the default
     *  configuration's; see the comment at the top of this file. */
    std::optional<std::string_view> resolve_string(uint32_t id) const;

    /** Value of resource @p id as a boolean: a `<bool>` resource, or any
     *  integer-typed value (nonzero is true), as Android's
     *  TypedArray.getBoolean reads it.  nullopt if the id is absent or its
     *  value is of another type, such as a string. */
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
    /** A single resolved resource entry (default-config value). */
    struct Entry
    {
        std::string type;
        std::string entry;
        uint8_t value_type = 0; ///< Res_value dataType, e.g. 0x03 string, 0x12 boolean.
        uint32_t value_data = 0;
        bool has_value = false; ///< false for complex/map entries.
    };

    /** The entry that holds the value of @p id, after following references
     *  to other resources; nullptr if there is none (an unknown id, a
     *  complex entry, or a chain of more than 20 references). */
    const Entry *value_entry(uint32_t id) const;

    void parse_package(std::span<const uint8_t> buf, std::size_t pkg_pos, std::size_t pkg_end);
    void parse_type_chunk(std::span<const uint8_t> buf, std::size_t pos, std::size_t end,
                          uint32_t package_id, const detail::StringPool &type_pool,
                          const detail::StringPool &key_pool);
    void record_entry(std::span<const uint8_t> buf, std::size_t pos, std::size_t end,
                      uint32_t res_id, const std::string &type_name,
                      const detail::StringPool &key_pool, bool is_default);

    std::string package_name_;
    std::vector<std::string> value_pool_; ///< Global value string pool.
    std::unordered_map<uint32_t, Entry> entries_;
    std::unordered_map<std::string, uint32_t> by_name_; ///< "type/entry" -> id.
};

} // namespace dex::apk
