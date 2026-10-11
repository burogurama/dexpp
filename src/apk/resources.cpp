#include "apk/resources.hpp"

#include <algorithm>

#include "apk/string_pool.hpp"

namespace dex::apk {

using detail::parse_string_pool;
using detail::rd16;
using detail::rd32;
using detail::StringPool;

namespace {

// Chunk types (ResourceTypes.h).
constexpr uint16_t RES_TABLE_TYPE = 0x0002;
constexpr uint16_t RES_STRING_POOL_TYPE = 0x0001;
constexpr uint16_t RES_TABLE_PACKAGE_TYPE = 0x0200;
constexpr uint16_t RES_TABLE_TYPE_TYPE = 0x0201;
constexpr uint16_t RES_TABLE_TYPESPEC_TYPE = 0x0202;

// ResTable_entry flags.
constexpr uint16_t ENTRY_FLAG_COMPLEX = 0x0001;
// ResTable_type flags.
constexpr uint8_t TYPE_FLAG_SPARSE = 0x01;
constexpr uint8_t TYPE_FLAG_OFFSET16 = 0x02;

constexpr uint8_t RES_VALUE_TYPE_STRING = 0x03;
constexpr uint32_t NO_ENTRY32 = 0xFFFFFFFF;

uint8_t rd8(std::span<const uint8_t> buf, std::size_t pos) { return buf[pos]; }

/** Read a package name from its fixed 128-UTF16 field. */
std::string read_package_name(std::span<const uint8_t> buf, std::size_t pos)
{
    std::string out;
    for (std::size_t i = 0; i < 128; ++i) {
        // The guard must track the index actually read (pos + i*2), not a
        // loop-invariant pos + 1.
        if (pos + i * 2 + 1 >= buf.size())
            break;
        uint16_t c = rd16(buf, pos + i * 2);
        if (c == 0)
            break;
        if (c < 0x80)
            out.push_back(static_cast<char>(c));
        else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
        else {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

/** Is this config the default (all bytes after the leading size are zero)? */
bool is_default_config(std::span<const uint8_t> buf, std::size_t cfg_pos, std::size_t end)
{
    if (cfg_pos + 4 > end)
        return false;
    uint32_t cfg_size = rd32(buf, cfg_pos);
    for (std::size_t i = 4; i < cfg_size && cfg_pos + i < end; ++i)
        if (buf[cfg_pos + i] != 0)
            return false;
    return true;
}

} // namespace

std::expected<ResourceTable, ResourceError> ResourceTable::parse(std::span<const uint8_t> buf)
{
    if (buf.size() < 12 || rd16(buf, 0) != RES_TABLE_TYPE)
        return std::unexpected(ResourceError{"not a resource table (bad chunk type)"});

    std::size_t total = std::min<std::size_t>(buf.size(), rd32(buf, 4));
    ResourceTable table;

    StringPool value_pool;
    std::size_t pos = rd16(buf, 2); // table header size

    while (pos + 8 <= total) {
        uint16_t ctype = rd16(buf, pos);
        uint32_t chunk_size = rd32(buf, pos + 4);
        if (chunk_size < 8 || pos + chunk_size > total)
            break;
        std::size_t next = pos + chunk_size;

        if (ctype == RES_STRING_POOL_TYPE && value_pool.strings.empty()) {
            value_pool = parse_string_pool(buf, pos, next);
        }
        else if (ctype == RES_TABLE_PACKAGE_TYPE) {
            table.parse_package(buf, pos, next);
        }
        pos = next;
    }

    table.value_pool_ = std::move(value_pool.strings);
    if (table.entries_.empty() && table.value_pool_.empty())
        return std::unexpected(ResourceError{"resource table has no packages or strings"});
    return table;
}

void ResourceTable::parse_package(std::span<const uint8_t> buf, std::size_t pkg_pos,
                                  std::size_t pkg_end)
{
    if (pkg_pos + 284 > pkg_end)
        return;
    uint32_t package_id = rd32(buf, pkg_pos + 8);
    if (package_name_.empty())
        package_name_ = read_package_name(buf, pkg_pos + 12);
    uint32_t type_strings_off = rd32(buf, pkg_pos + 268);
    uint32_t key_strings_off = rd32(buf, pkg_pos + 276);

    StringPool type_pool;
    StringPool key_pool;
    if (type_strings_off != 0)
        type_pool = parse_string_pool(buf, pkg_pos + type_strings_off, pkg_end);
    if (key_strings_off != 0)
        key_pool = parse_string_pool(buf, pkg_pos + key_strings_off, pkg_end);

    // Walk the package's nested chunks: typespec and type chunks.
    std::size_t pos = pkg_pos + rd16(buf, pkg_pos + 2); // package header size
    while (pos + 8 <= pkg_end) {
        uint16_t ctype = rd16(buf, pos);
        uint32_t chunk_size = rd32(buf, pos + 4);
        if (chunk_size < 8 || pos + chunk_size > pkg_end)
            break;
        if (ctype == RES_TABLE_TYPE_TYPE)
            parse_type_chunk(buf, pos, pos + chunk_size, package_id, type_pool, key_pool);
        pos += chunk_size;
    }
}

void ResourceTable::parse_type_chunk(std::span<const uint8_t> buf, std::size_t pos, std::size_t end,
                                     uint32_t package_id, const detail::StringPool &type_pool,
                                     const detail::StringPool &key_pool)
{
    if (pos + 20 > end)
        return;
    uint16_t header_size = rd16(buf, pos + 2);
    uint8_t type_id = rd8(buf, pos + 8); // 1-based
    uint8_t flags = rd8(buf, pos + 9);
    uint32_t entry_count = rd32(buf, pos + 12);
    uint32_t entries_start = rd32(buf, pos + 16);

    std::size_t cfg_pos = pos + 20;
    bool is_default = is_default_config(buf, cfg_pos, end);
    std::string type_name(type_pool.get(type_id - 1));

    std::size_t index_at = pos + header_size;
    std::size_t data_base = pos + entries_start;
    bool sparse = (flags & TYPE_FLAG_SPARSE) != 0;
    bool offset16 = (flags & TYPE_FLAG_OFFSET16) != 0;

    for (uint32_t i = 0; i < entry_count; ++i) {
        uint32_t entry_index = i;
        uint32_t entry_off = NO_ENTRY32;

        if (sparse) {
            std::size_t e = index_at + static_cast<std::size_t>(i) * 4;
            if (e + 4 > end)
                break;
            entry_index = rd16(buf, e);
            entry_off = static_cast<uint32_t>(rd16(buf, e + 2)) * 4u;
        }
        else if (offset16) {
            std::size_t e = index_at + static_cast<std::size_t>(i) * 2;
            if (e + 2 > end)
                break;
            uint16_t o = rd16(buf, e);
            entry_off = (o == 0xFFFF) ? NO_ENTRY32 : static_cast<uint32_t>(o) * 2u;
        }
        else {
            std::size_t e = index_at + static_cast<std::size_t>(i) * 4;
            if (e + 4 > end)
                break;
            entry_off = rd32(buf, e);
        }
        if (entry_off == NO_ENTRY32)
            continue;

        uint32_t res_id = (package_id << 24) | (static_cast<uint32_t>(type_id) << 16) | entry_index;
        record_entry(buf, data_base + entry_off, end, res_id, type_name, key_pool, is_default);
    }
}

void ResourceTable::record_entry(std::span<const uint8_t> buf, std::size_t pos, std::size_t end,
                                 uint32_t res_id, const std::string &type_name,
                                 const detail::StringPool &key_pool, bool is_default)
{
    if (pos + 8 > end)
        return;
    uint16_t entry_size = rd16(buf, pos);
    uint16_t entry_flags = rd16(buf, pos + 2);
    uint32_t key_idx = rd32(buf, pos + 4);

    // A previously recorded default-config entry wins over later configs.
    auto existing = entries_.find(res_id);
    if (existing != entries_.end() && !is_default)
        return;

    Entry e;
    e.type = type_name;
    e.entry = std::string(key_pool.get(key_idx));

    if (!(entry_flags & ENTRY_FLAG_COMPLEX)) {
        std::size_t val_pos = pos + entry_size; // Res_value follows the entry header
        if (val_pos + 8 <= end) {
            e.value_type = rd8(buf, val_pos + 3);
            e.value_data = rd32(buf, val_pos + 4);
            e.has_value = true;
        }
    }
    // Complex (map) entries are recorded by name only; no scalar value.

    by_name_[type_name + "/" + e.entry] = res_id;
    entries_[res_id] = std::move(e);
}

std::optional<ResourceName> ResourceTable::name_of(uint32_t id) const
{
    auto it = entries_.find(id);
    if (it == entries_.end())
        return std::nullopt;
    return ResourceName{package_name_, it->second.type, it->second.entry};
}

std::optional<std::string_view> ResourceTable::resolve_string(uint32_t id) const
{
    auto it = entries_.find(id);
    if (it == entries_.end() || !it->second.has_value)
        return std::nullopt;
    if (it->second.value_type != RES_VALUE_TYPE_STRING)
        return std::nullopt;
    uint32_t sidx = it->second.value_data;
    if (sidx >= value_pool_.size())
        return std::nullopt;
    return value_pool_[sidx];
}

std::optional<uint32_t> ResourceTable::id_of(std::string_view type, std::string_view entry) const
{
    std::string key = std::string(type) + "/" + std::string(entry);
    auto it = by_name_.find(key);
    if (it == by_name_.end())
        return std::nullopt;
    return it->second;
}

} // namespace dex::apk
