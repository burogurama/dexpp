#pragma once

/** Parser for resources.arsc, the compiled resource table inside an APK.
 *
 *  Scope: forward resolution of a resource id (0xPPTTEEEE) to its name and, for
 *  string resources, its value — enough to resolve manifest references like
 *  android:label="@string/app_name".  Configuration variants (locale, density)
 *  are collapsed to a single value, preferring the default (config-less)
 *  entry. */

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dex::apk {

namespace detail {
struct StringPool;
} // namespace detail

struct ResourceError
{
    std::string message;
};

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
    /** Decode a resources.arsc image. */
    static std::expected<ResourceTable, ResourceError> parse(std::span<const uint8_t> buf);

    /** Name of the resource with id @p id, or nullopt if not present. */
    std::optional<ResourceName> name_of(uint32_t id) const;

    /** Value of a string-typed resource @p id, resolved through the global
     *  value pool.  nullopt if the id is absent or its value is not a string. */
    std::optional<std::string_view> resolve_string(uint32_t id) const;

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
        uint8_t value_type = 0; ///< Res_value dataType; 0x03 == string.
        uint32_t value_data = 0;
        bool has_value = false; ///< false for complex/map entries.
    };

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
