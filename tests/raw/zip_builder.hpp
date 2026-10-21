#pragma once

// Builds small ZIP archives in memory for tests, with full control over what
// the central directory declares — so a test can craft size mismatches,
// duplicate names, or unsupported methods that no real zip tool would write.
//
// DEFLATE payloads are made of "stored" blocks (BTYPE=00: the raw bytes plus a
// length header), which is valid DEFLATE that any inflater accepts, so no
// compression library is needed.  CRC-32 fields are left 0: the reader does
// not check them.

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

struct ZipEntrySpec
{
    std::string name;
    uint16_t method;              ///< 0 = stored, 8 = deflate, anything else as is
    std::vector<uint8_t> payload; ///< Written as the entry's data, unchanged
    uint32_t uncompressed_size;   ///< As declared in both headers
};

inline void zip_put16(std::vector<uint8_t> &b, uint16_t v)
{
    b.push_back(v & 0xFF);
    b.push_back(v >> 8);
}

inline void zip_put32(std::vector<uint8_t> &b, uint32_t v)
{
    zip_put16(b, v & 0xFFFF);
    zip_put16(b, v >> 16);
}

/** Raw DEFLATE stream holding @p data in stored blocks of at most 65535 bytes. */
inline std::vector<uint8_t> deflate_stored_blocks(std::span<const uint8_t> data)
{
    std::vector<uint8_t> out;
    std::size_t pos = 0;
    do {
        auto len = static_cast<uint16_t>(std::min<std::size_t>(data.size() - pos, 0xFFFF));
        bool final = pos + len == data.size();
        out.push_back(final ? 0x01 : 0x00); // BFINAL bit, BTYPE=00
        zip_put16(out, len);
        zip_put16(out, static_cast<uint16_t>(~len));
        out.insert(out.end(), data.begin() + pos, data.begin() + pos + len);
        pos += len;
    } while (pos < data.size());
    return out;
}

inline ZipEntrySpec stored_entry(std::string name, std::vector<uint8_t> data)
{
    auto size = static_cast<uint32_t>(data.size());
    return {std::move(name), 0, std::move(data), size};
}

inline ZipEntrySpec deflated_entry(std::string name, const std::vector<uint8_t> &data)
{
    return {std::move(name), 8, deflate_stored_blocks(data), static_cast<uint32_t>(data.size())};
}

inline std::vector<uint8_t> build_zip(const std::vector<ZipEntrySpec> &entries)
{
    std::vector<uint8_t> zip;
    std::vector<uint32_t> local_offsets;
    for (const auto &e : entries) {
        local_offsets.push_back(static_cast<uint32_t>(zip.size()));
        zip_put32(zip, 0x04034b50); // local file header signature
        zip_put16(zip, 20);         // version needed
        zip_put16(zip, 0);          // flags
        zip_put16(zip, e.method);
        zip_put32(zip, 0); // mod time + date
        zip_put32(zip, 0); // crc-32
        zip_put32(zip, static_cast<uint32_t>(e.payload.size()));
        zip_put32(zip, e.uncompressed_size);
        zip_put16(zip, static_cast<uint16_t>(e.name.size()));
        zip_put16(zip, 0); // extra length
        zip.insert(zip.end(), e.name.begin(), e.name.end());
        zip.insert(zip.end(), e.payload.begin(), e.payload.end());
    }

    auto cd_offset = static_cast<uint32_t>(zip.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto &e = entries[i];
        zip_put32(zip, 0x02014b50); // central directory signature
        zip_put16(zip, 20);         // version made by
        zip_put16(zip, 20);         // version needed
        zip_put16(zip, 0);          // flags
        zip_put16(zip, e.method);
        zip_put32(zip, 0); // mod time + date
        zip_put32(zip, 0); // crc-32
        zip_put32(zip, static_cast<uint32_t>(e.payload.size()));
        zip_put32(zip, e.uncompressed_size);
        zip_put16(zip, static_cast<uint16_t>(e.name.size()));
        zip_put16(zip, 0); // extra length
        zip_put16(zip, 0); // comment length
        zip_put16(zip, 0); // disk number
        zip_put16(zip, 0); // internal attributes
        zip_put32(zip, 0); // external attributes
        zip_put32(zip, local_offsets[i]);
        zip.insert(zip.end(), e.name.begin(), e.name.end());
    }
    auto cd_size = static_cast<uint32_t>(zip.size() - cd_offset);

    zip_put32(zip, 0x06054b50); // end of central directory signature
    zip_put16(zip, 0);          // this disk
    zip_put16(zip, 0);          // disk with the central directory
    zip_put16(zip, static_cast<uint16_t>(entries.size()));
    zip_put16(zip, static_cast<uint16_t>(entries.size()));
    zip_put32(zip, cd_size);
    zip_put32(zip, cd_offset);
    zip_put16(zip, 0); // comment length
    return zip;
}
