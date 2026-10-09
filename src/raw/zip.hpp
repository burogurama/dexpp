#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dex::raw::zip {

/** Error from ZIP central-directory parsing or entry extraction. */
struct ZipError
{
    std::string message;
};

/** One central-directory record.  Sizes and the local-header offset are as
 *  the archive declares them — untrusted until read_entry() validates them. */
struct Entry
{
    std::string name;
    uint16_t method; ///< 0 = stored, 8 = deflate; anything else is unsupported.
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint32_t local_header_offset;
};

/** Every central-directory record, in directory order.  Duplicate names are
 *  kept as they appear.  Fails the same way entry_names() does. */
std::expected<std::vector<Entry>, ZipError> entries(std::span<const uint8_t> buf);

/** Names of every entry in the archive's central directory, in directory
 *  order.  Fails if the buffer is not a ZIP archive (no end-of-central-
 *  directory record) or the directory is structurally invalid.
 *
 *  Limitations (sufficient for APKs): no ZIP64, no encryption, and only
 *  compression methods 0 (stored) and 8 (deflate) are extractable. */
std::expected<std::vector<std::string>, ZipError> entry_names(std::span<const uint8_t> buf);

/** Decompressed contents of the entry named exactly @p name (the first one,
 *  if the name repeats).  Sizes are taken from the central directory, so
 *  archives using streaming data descriptors extract correctly. */
std::expected<std::vector<uint8_t>, ZipError> read_entry(std::span<const uint8_t> buf,
                                                         std::string_view name);

/** Decompressed contents of @p entry, a record from entries().  Skips the
 *  central-directory walk of the name-based overload; every bounds and size
 *  check still applies, so a hand-built Entry is safe to pass.  Safe to call
 *  from several threads at once. */
std::expected<std::vector<uint8_t>, ZipError> read_entry(std::span<const uint8_t> buf,
                                                         const Entry &entry);

/** Byte offset of the central directory, from the end-of-central-directory
 *  record.  The APK Signing Block, when present, ends exactly here. */
std::expected<uint32_t, ZipError> central_directory_offset(std::span<const uint8_t> buf);

} // namespace dex::raw::zip
