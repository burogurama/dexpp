#include "apk/resources.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <initializer_list>
#include <iterator>
#include <unordered_set>
#include <utility>

#include "apk/resource_values.hpp"
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

constexpr uint32_t NO_ENTRY32 = 0xFFFFFFFF;

// How many references a lookup follows before it gives up: the limit
// Android's AssetManager2::ResolveReference uses.
constexpr int kMaxReferenceSteps = 20;

// The largest ResTable_config dexpp reads (sizeof(ResTable_config) in
// Android 14).  A longer one has fields dexpp does not know; they are left
// out of the configuration's name.
constexpr std::size_t kConfigBytes = 64;

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

/** A ResTable_config, copied into zero-filled bytes.  Android reads only the
 *  first `size` bytes of the struct and treats the fields after them as 0
 *  ("any"), so this does the same. */
using ConfigBytes = std::array<uint8_t, kConfigBytes>;

/** The config at @p cfg_pos, or nullopt when even its size field does not
 *  fit before @p end. */
std::optional<ConfigBytes> read_config(std::span<const uint8_t> buf, std::size_t cfg_pos,
                                       std::size_t end)
{
    if (cfg_pos > end || end - cfg_pos < 4)
        return std::nullopt;
    ConfigBytes config{};
    std::size_t size = std::min<std::size_t>(rd32(buf, cfg_pos), kConfigBytes);
    size = std::min(size, end - cfg_pos);
    for (std::size_t i = 4; i < size; ++i)
        config[i] = buf[cfg_pos + i];
    return config;
}

/** True for the default configuration: every field after the size is 0. */
bool is_default_config(const ConfigBytes &config)
{
    return std::all_of(config.begin() + 4, config.end(), [](uint8_t b) { return b == 0; });
}

uint16_t field16(const ConfigBytes &c, std::size_t at)
{
    return static_cast<uint16_t>(c[at] | (c[at + 1] << 8));
}

/** Append @p count bytes from @p at, stopping at the first NUL.  A byte
 *  that is not printable ASCII becomes '?', so the name is always valid
 *  UTF-8 whatever the table holds. */
void append_chars(std::string &out, const ConfigBytes &c, std::size_t at, std::size_t count)
{
    for (std::size_t i = 0; i < count && c[at + i] != 0; ++i) {
        const uint8_t b = c[at + i];
        out.push_back(b >= 0x21 && b <= 0x7e ? static_cast<char>(b) : '?');
    }
}

/** A language (base 'a') or region (base '0') code: two letters, or three
 *  letters packed into two bytes when the high bit of the first is set
 *  (ResourceTypes.cpp, unpackLanguageOrRegion). */
void append_language_or_region(std::string &out, const ConfigBytes &c, std::size_t at, char base)
{
    const uint8_t first = c[at];
    const uint8_t second = c[at + 1];
    if (first & 0x80) {
        const uint8_t letters[3] = {
            static_cast<uint8_t>(second & 0x1f),
            static_cast<uint8_t>(((second & 0xe0) >> 5) + ((first & 0x03) << 3)),
            static_cast<uint8_t>((first & 0x7c) >> 2),
        };
        ConfigBytes chars{};
        for (int i = 0; i < 3; ++i)
            chars[i] = static_cast<uint8_t>(letters[i] + base);
        append_chars(out, chars, 0, 3);
        return;
    }
    append_chars(out, c, at, 2);
}

/** Add the name @p names[index] of a configuration field to @p out, after a
 *  '-'.  Index 0 is "any" and adds nothing.  An index with no name (past the
 *  end of @p names, or an empty name) is written "<label>=<raw>", as
 *  ResTable_config::toString() writes it. */
void add_named(std::string &out, unsigned index, unsigned raw,
               std::initializer_list<std::string_view> names, std::string_view label)
{
    if (index == 0)
        return;
    if (!out.empty())
        out.push_back('-');
    if (index < names.size() && !names.begin()[index].empty())
        out.append(names.begin()[index]);
    else
        out += std::format("{}={}", label, raw);
}

/** The configuration's name, written like the qualifiers of a res/ folder,
 *  the way ResTable_config::toString() writes it (ResourceTypes.cpp), which
 *  is what `aapt2 dump resources` prints.  "" for the default
 *  configuration.  The byte offsets are those of struct ResTable_config. */
std::string config_name(const ConfigBytes &c)
{
    std::string out;
    auto add = [&out](std::string_view part) {
        if (!out.empty())
            out.push_back('-');
        out.append(part);
    };
    auto field = [&c](std::size_t at, uint8_t mask, int shift) {
        return static_cast<unsigned>((c[at] & mask) >> shift);
    };
    auto raw = [&c](std::size_t at, uint8_t mask) { return static_cast<unsigned>(c[at] & mask); };

    if (const uint16_t mcc = field16(c, 4))
        add(std::format("mcc{}", mcc));
    if (const uint16_t mnc = field16(c, 6))
        add(std::format("mnc{}", mnc));

    // Locale: the legacy "fr-rCA" form, or the BCP 47 form "b+sr+Latn" when
    // the locale has a script, a variant or a numbering system.
    if (c[8] != 0) {
        const bool has_script = c[36] != 0 && c[52] == 0; // given, not computed
        std::string locale;
        if (!has_script && c[40] == 0 && c[53] == 0) {
            append_language_or_region(locale, c, 8, 'a');
            if (c[10] != 0) {
                locale += "-r";
                append_language_or_region(locale, c, 10, '0');
            }
        }
        else {
            locale = "b+";
            append_language_or_region(locale, c, 8, 'a');
            if (has_script) {
                locale += "+";
                append_chars(locale, c, 36, 4);
            }
            if (c[10] != 0) {
                locale += "+";
                append_language_or_region(locale, c, 10, '0');
            }
            if (c[40] != 0) {
                locale += "+";
                append_chars(locale, c, 40, 8);
            }
            if (c[53] != 0) {
                locale += "+u+nu+";
                append_chars(locale, c, 53, 8);
            }
        }
        add(locale);
    }

    add_named(out, field(19, 0x03, 0), raw(19, 0x03), {"", "neuter", "feminine", "masculine"},
              "grammaticalGender");
    add_named(out, field(28, 0xc0, 6), raw(28, 0xc0), {"", "ldltr", "ldrtl"}, "layoutDir");
    if (const uint16_t sw = field16(c, 30))
        add(std::format("sw{}dp", sw));
    if (const uint16_t w = field16(c, 32))
        add(std::format("w{}dp", w));
    if (const uint16_t h = field16(c, 34))
        add(std::format("h{}dp", h));
    add_named(out, field(28, 0x0f, 0), raw(28, 0x0f), {"", "small", "normal", "large", "xlarge"},
              "screenLayoutSize");
    add_named(out, field(28, 0x30, 4), raw(28, 0x30), {"", "notlong", "long"}, "screenLayoutLong");
    add_named(out, field(48, 0x03, 0), raw(48, 0x03), {"", "notround", "round"}, "screenRound");
    add_named(out, field(49, 0x03, 0), raw(49, 0x03), {"", "nowidecg", "widecg"}, "wideColorGamut");
    add_named(out, field(49, 0x0c, 2), raw(49, 0x0c), {"", "lowdr", "highdr"}, "hdr");
    add_named(out, c[12], c[12], {"", "port", "land", "square"}, "orientation");
    add_named(out, field(29, 0x0f, 0), raw(29, 0x0f),
              {"", "", "desk", "car", "television", "appliance", "watch", "vrheadset"},
              "uiModeType");
    add_named(out, field(29, 0x30, 4), raw(29, 0x30), {"", "notnight", "night"}, "uiModeNight");

    // Density: a number of dots per inch, with names for the usual ones.
    if (const uint16_t density = field16(c, 14)) {
        constexpr std::pair<uint16_t, std::string_view> kDensities[] = {
            {120, "ldpi"},    {160, "mdpi"},     {213, "tvdpi"},
            {240, "hdpi"},    {320, "xhdpi"},    {480, "xxhdpi"},
            {640, "xxxhdpi"}, {0xffff, "nodpi"}, {0xfffe, "anydpi"},
        };
        const auto *named = std::find_if(std::begin(kDensities), std::end(kDensities),
                                         [&](const auto &d) { return d.first == density; });
        add(named != std::end(kDensities) ? std::string(named->second)
                                          : std::format("{}dpi", density));
    }

    add_named(out, c[13], c[13], {"", "notouch", "stylus", "finger"}, "touchscreen");
    add_named(out, field(18, 0x03, 0), raw(18, 0x03), {"", "keysexposed", "keyshidden", "keyssoft"},
              "keysHidden");
    add_named(out, c[16], c[16], {"", "nokeys", "qwerty", "12key"}, "keyboard");
    add_named(out, field(18, 0x0c, 2), raw(18, 0x0c), {"", "navexposed", "navhidden"},
              "inputFlagsNavHidden");
    add_named(out, c[17], c[17], {"", "nonav", "dpad", "trackball", "wheel"}, "navigation");
    if (field16(c, 20) != 0 || field16(c, 22) != 0)
        add(std::format("{}x{}", field16(c, 20), field16(c, 22)));
    if (field16(c, 24) != 0 || field16(c, 26) != 0) {
        add(std::format("v{}", field16(c, 24)));
        if (const uint16_t minor = field16(c, 26))
            out += std::format(".{}", minor);
    }
    return out;
}

} // namespace

// ---- reading values -------------------------------------------------------

namespace detail {

std::optional<bool> boolean_value(uint8_t type, uint32_t data, std::optional<std::string_view> text)
{
    if (is_int_type(type))
        return data != 0;
    switch (type) {
    case kTypeString:
        if (!text.has_value() || text->empty())
            return std::nullopt; // convertValueToBoolean: the default
        return *text == "1" || *text == "true" || *text == "TRUE";
    case kTypeFloat:
    case kTypeDimension:
    case kTypeFraction:
        // coerceToString() gives "1.0", "1.0dip", "100.0%": never "1",
        // "true" or "TRUE".
        return false;
    default:
        // A null value, a reference or attribute (resolved by the caller),
        // or a type coerceToString() gives no text for.
        return std::nullopt;
    }
}

EveryValue ResourceTableAccess::every_value(const ResourceTable &table, uint32_t id,
                                            std::size_t limit)
{
    EveryValue out;
    std::vector<uint32_t> frontier{id};
    std::unordered_set<uint64_t> distinct;

    // Breadth first: the resources at `depth` are reached through `depth`
    // references.  Android reads up to 21 resources in a chain (the first,
    // then 20 references), as value_entry() does.  A resource is looked at
    // again when a longer path reaches it, so a loop runs into that limit
    // and counts as no value, as it does for value_entry().
    for (int depth = 0; !frontier.empty(); ++depth) {
        if (depth > kMaxReferenceSteps) {
            out.incomplete = true;
            break;
        }
        std::vector<uint32_t> next;
        std::unordered_set<uint32_t> in_next;
        for (uint32_t current : frontier) {
            out.spec_flags |= table.spec_flags(current);
            auto it = table.entries_.find(current);
            if (it == table.entries_.end()) {
                out.incomplete = true;
                continue;
            }
            for (const ResourceTable::Value &v : it->second.values) {
                if (++out.visited > limit) {
                    out.incomplete = true;
                    return out;
                }
                if (!v.has_value || (is_resource_reference(v.type) && v.data == 0)) {
                    // A complex entry, or @null.
                    out.incomplete = true;
                    continue;
                }
                if (is_resource_reference(v.type)) {
                    if (in_next.insert(v.data).second)
                        next.push_back(v.data);
                    continue;
                }
                if (distinct.insert((uint64_t{v.type} << 32) | v.data).second)
                    out.values.push_back(FinalValue{v.type, v.data});
            }
        }
        frontier = std::move(next);
    }
    if (out.values.empty())
        out.incomplete = true;
    return out;
}

std::optional<FinalValue> ResourceTableAccess::default_value(const ResourceTable &table,
                                                             uint32_t id)
{
    const ResourceTable::Value *v = table.value_entry(id);
    if (!v)
        return std::nullopt;
    return FinalValue{v->type, v->data};
}

std::optional<std::string_view> ResourceTableAccess::pool_string(const ResourceTable &table,
                                                                 uint32_t index)
{
    if (index >= table.value_pool_.size())
        return std::nullopt;
    return table.value_pool_[index];
}

} // namespace detail

std::optional<bool> ResourceValue::as_bool() const
{
    std::optional<std::string_view> text;
    if (string.has_value())
        text = *string;
    return detail::boolean_value(type, data, text);
}

std::optional<int64_t> ResourceValue::as_int() const
{
    if (!detail::is_int_type(type))
        return std::nullopt;
    return static_cast<int32_t>(data);
}

// ---- parsing --------------------------------------------------------------

std::expected<ResourceTable, Error> ResourceTable::parse(std::span<const uint8_t> buf)
{
    if (buf.size() < 12 || rd16(buf, 0) != RES_TABLE_TYPE)
        return std::unexpected(
            Error{ErrorCode::InvalidResources, "not a resource table (bad chunk type)"});

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
        return std::unexpected(
            Error{ErrorCode::InvalidResources, "resource table has no packages or strings"});
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
        if (ctype == RES_TABLE_TYPESPEC_TYPE)
            parse_type_spec_chunk(buf, pos, pos + chunk_size, package_id);
        else if (ctype == RES_TABLE_TYPE_TYPE)
            parse_type_chunk(buf, pos, pos + chunk_size, package_id, type_pool, key_pool);
        pos += chunk_size;
    }
}

void ResourceTable::parse_type_spec_chunk(std::span<const uint8_t> buf, std::size_t pos,
                                          std::size_t end, uint32_t package_id)
{
    // ResTable_typeSpec: the chunk header, id (u8), two reserved fields,
    // entryCount (u32), then one u32 of flags per entry.  Android reads the
    // flags right after this 16-byte struct.
    constexpr std::size_t kHeader = 16;
    if (end - pos < kHeader)
        return;
    const uint8_t type_id = rd8(buf, pos + 8);
    if (type_id == 0)
        return;
    // Android keeps the first typeSpec of a type and ignores later ones.
    const uint32_t key = ((package_id & 0xff) << 8) | type_id;
    if (spec_flags_.contains(key))
        return;
    // The count comes from the file: never read past the chunk.  Android
    // allows at most 2^16 entries in a type.
    const std::size_t available = (end - pos - kHeader) / 4;
    const std::size_t count =
        std::min({static_cast<std::size_t>(rd32(buf, pos + 12)), available, std::size_t{65536}});
    std::vector<uint32_t> flags(count);
    for (std::size_t i = 0; i < count; ++i)
        flags[i] = rd32(buf, pos + kHeader + i * 4);
    spec_flags_.emplace(key, std::move(flags));
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

    std::optional<ConfigBytes> config_bytes = read_config(buf, pos + 20, end);
    if (!config_bytes.has_value())
        return;
    const bool is_default = is_default_config(*config_bytes);
    const uint32_t config = intern_config(config_name(*config_bytes));
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
        record_entry(buf, data_base + entry_off, end, res_id, type_name, key_pool, config,
                     is_default);
    }
}

void ResourceTable::record_entry(std::span<const uint8_t> buf, std::size_t pos, std::size_t end,
                                 uint32_t res_id, const std::string &type_name,
                                 const detail::StringPool &key_pool, uint32_t config,
                                 bool is_default)
{
    if (pos + 8 > end)
        return;
    uint16_t entry_size = rd16(buf, pos);
    uint16_t entry_flags = rd16(buf, pos + 2);
    uint32_t key_idx = rd32(buf, pos + 4);

    Value value;
    value.config = config;
    if (!(entry_flags & ENTRY_FLAG_COMPLEX)) {
        std::size_t val_pos = pos + entry_size; // Res_value follows the entry header
        if (val_pos + 8 <= end) {
            value.type = rd8(buf, val_pos + 3);
            value.data = rd32(buf, val_pos + 4);
            value.has_value = true;
        }
    }
    // Complex (map) entries are recorded by name only; no scalar value.

    auto [it, inserted] = entries_.try_emplace(res_id);
    Entry &e = it->second;
    e.values.push_back(value);
    // The name and the value resolve_* use come from the first default
    // configuration value, or from the first value when there is none yet.
    // Android also takes the first of two equal configurations.
    if (inserted || (is_default && !e.has_default)) {
        e.primary = e.values.size() - 1;
        e.has_default = is_default;
        e.type = type_name;
        e.entry = std::string(key_pool.get(key_idx));
        by_name_[type_name + "/" + e.entry] = res_id;
    }
}

uint32_t ResourceTable::intern_config(std::string name)
{
    auto [it, inserted] = config_index_.try_emplace(name, static_cast<uint32_t>(configs_.size()));
    if (inserted)
        configs_.push_back(std::move(name));
    return it->second;
}

// ---- lookups --------------------------------------------------------------

std::optional<ResourceName> ResourceTable::name_of(uint32_t id) const
{
    auto it = entries_.find(id);
    if (it == entries_.end())
        return std::nullopt;
    return ResourceName{package_name_, it->second.type, it->second.entry};
}

std::vector<ResourceValue> ResourceTable::values(uint32_t id) const
{
    std::vector<ResourceValue> out;
    auto it = entries_.find(id);
    if (it == entries_.end())
        return out;
    for (const Value &v : it->second.values) {
        if (!v.has_value)
            continue;
        ResourceValue rv;
        rv.configuration = configs_[v.config];
        rv.type = v.type;
        rv.data = v.data;
        if (v.type == detail::kTypeString && v.data < value_pool_.size())
            rv.string = value_pool_[v.data];
        out.push_back(std::move(rv));
    }
    return out;
}

const ResourceTable::Value *ResourceTable::value_entry(uint32_t id) const
{
    for (int step = 0; step <= kMaxReferenceSteps; ++step) {
        auto it = entries_.find(id);
        if (it == entries_.end())
            return nullptr;
        const Value &v = it->second.values[it->second.primary];
        if (!v.has_value)
            return nullptr;
        if (!detail::is_resource_reference(v.type))
            return &v;
        id = v.data;
    }
    return nullptr; // a reference loop, or a longer chain than Android follows
}

uint32_t ResourceTable::spec_flags(uint32_t id) const
{
    const uint32_t key = ((id >> 24) << 8) | ((id >> 16) & 0xff);
    auto it = spec_flags_.find(key);
    if (it == spec_flags_.end())
        return 0;
    const uint32_t entry = id & 0xffff;
    return entry < it->second.size() ? it->second[entry] : 0;
}

std::optional<std::string_view> ResourceTable::resolve_string(uint32_t id) const
{
    const Value *v = value_entry(id);
    if (!v || v->type != detail::kTypeString)
        return std::nullopt;
    if (v->data >= value_pool_.size())
        return std::nullopt;
    return value_pool_[v->data];
}

std::optional<bool> ResourceTable::resolve_bool(uint32_t id) const
{
    const Value *v = value_entry(id);
    if (!v)
        return std::nullopt;
    std::optional<std::string_view> text;
    if (v->type == detail::kTypeString && v->data < value_pool_.size())
        text = value_pool_[v->data];
    return detail::boolean_value(v->type, v->data, text);
}

std::optional<int64_t> ResourceTable::resolve_int(uint32_t id) const
{
    const Value *v = value_entry(id);
    if (!v || !detail::is_int_type(v->type))
        return std::nullopt;
    return static_cast<int32_t>(v->data);
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
