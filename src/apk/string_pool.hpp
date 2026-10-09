#pragma once

/** Shared decoder for the RES_STRING_POOL chunk, used by both the binary-XML
 *  (axml) and resource-table (resources) parsers — the chunk format is the
 *  same in both. */

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dex::apk::detail {

constexpr uint32_t kNoEntry = 0xFFFFFFFF;

inline uint16_t rd16(std::span<const uint8_t> buf, std::size_t pos)
{
    return static_cast<uint16_t>(buf[pos]) | (static_cast<uint16_t>(buf[pos + 1]) << 8);
}

inline uint32_t rd32(std::span<const uint8_t> buf, std::size_t pos)
{
    return static_cast<uint32_t>(buf[pos]) | (static_cast<uint32_t>(buf[pos + 1]) << 8) |
           (static_cast<uint32_t>(buf[pos + 2]) << 16) |
           (static_cast<uint32_t>(buf[pos + 3]) << 24);
}

/** Decoded string pool: index → UTF-8 string. */
struct StringPool
{
    std::vector<std::string> strings;

    std::string_view get(uint32_t idx) const
    {
        if (idx == kNoEntry || idx >= strings.size())
            return {};
        return strings[idx];
    }
};

/** Decode the RES_STRING_POOL chunk whose header begins at @p pos, bounded by
 *  @p end.  Handles both UTF-8 and UTF-16 pools; malformed entries decode to
 *  empty strings while keeping indices aligned. */
StringPool parse_string_pool(std::span<const uint8_t> buf, std::size_t pos, std::size_t end);

} // namespace dex::apk::detail
