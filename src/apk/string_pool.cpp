#include "apk/string_pool.hpp"
#include <algorithm>

namespace dex::apk::detail {

namespace {

constexpr uint32_t UTF8_FLAG = 1u << 8;

void append_utf8(std::string &out, uint32_t cp)
{
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    }
    else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string decode_utf16(std::span<const uint8_t> buf, std::size_t pos, uint32_t len)
{
    std::string out;
    // `len` is an attacker-controlled UTF-16 length (up to ~2^31 via the
    // high-bit extension); never reserve more code units than the buffer can
    // actually supply, or a bogus length triggers a multi-gigabyte allocation.
    // The decode loop below independently stops at the buffer end.
    std::size_t max_units = pos < buf.size() ? (buf.size() - pos) / 2 : 0;
    out.reserve(std::min<std::size_t>(len, max_units));
    for (uint32_t i = 0; i < len && pos + 1 < buf.size(); ++i, pos += 2) {
        uint32_t u = rd16(buf, pos);
        if (u >= 0xD800 && u < 0xDC00 && i + 1 < len && pos + 3 < buf.size()) {
            uint32_t lo = rd16(buf, pos + 2);
            if (lo >= 0xDC00 && lo < 0xE000) {
                u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                ++i;
                pos += 2;
            }
        }
        append_utf8(out, u);
    }
    return out;
}

} // namespace

StringPool parse_string_pool(std::span<const uint8_t> buf, std::size_t pos, std::size_t end)
{
    StringPool pool;
    if (pos + 28 > end)
        return pool;

    uint16_t header_size = rd16(buf, pos + 2);
    uint32_t count = rd32(buf, pos + 8);
    uint32_t flags = rd32(buf, pos + 16);
    uint32_t strings_start = rd32(buf, pos + 20);
    bool utf8 = (flags & UTF8_FLAG) != 0;

    std::size_t offsets_at = pos + header_size;
    std::size_t data_base = pos + strings_start;
    if (offsets_at + static_cast<std::size_t>(count) * 4 > end || data_base > end)
        return pool;

    pool.strings.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        std::size_t p = data_base + rd32(buf, offsets_at + static_cast<std::size_t>(i) * 4);
        if (p >= end) {
            pool.strings.emplace_back(); // keep indices aligned
            continue;
        }

        if (utf8) {
            // Two lengths precede the bytes: UTF-16 length (skipped) then byte
            // length; each is 1 or 2 bytes with the high bit as continuation.
            if (buf[p] & 0x80)
                p += 2;
            else
                p += 1;
            if (p >= end) {
                pool.strings.emplace_back();
                continue;
            }
            uint32_t byte_len = buf[p];
            if (byte_len & 0x80) {
                if (p + 1 >= end) {
                    pool.strings.emplace_back();
                    continue;
                }
                byte_len = ((byte_len & 0x7F) << 8) | buf[p + 1];
                p += 2;
            }
            else {
                p += 1;
            }
            if (p + byte_len > end) {
                pool.strings.emplace_back();
                continue;
            }
            pool.strings.emplace_back(reinterpret_cast<const char *>(buf.data() + p), byte_len);
        }
        else {
            if (p + 2 > end) {
                pool.strings.emplace_back();
                continue;
            }
            uint32_t len = rd16(buf, p);
            p += 2;
            if (len & 0x8000) {
                if (p + 2 > end) {
                    pool.strings.emplace_back();
                    continue;
                }
                len = ((len & 0x7FFF) << 16) | rd16(buf, p);
                p += 2;
            }
            pool.strings.push_back(decode_utf16(buf, p, len));
        }
    }
    return pool;
}

} // namespace dex::apk::detail
