#include "zip.hpp"

#include <memory>
#include <mutex>
#include <new>
#include <optional>

#include <libdeflate.h>

namespace dex::raw::zip {

namespace {

constexpr uint32_t EOCD_SIG = 0x06054b50;
constexpr uint32_t CDIR_SIG = 0x02014b50;
constexpr uint32_t LOCAL_SIG = 0x04034b50;
constexpr std::size_t EOCD_MIN_SIZE = 22;
// EOCD must sit within the trailing comment's maximum reach (65535) + itself.
constexpr std::size_t EOCD_MAX_SCAN = 65535 + EOCD_MIN_SIZE;

uint16_t rd16(std::span<const uint8_t> buf, std::size_t pos)
{
    return static_cast<uint16_t>(buf[pos]) | (static_cast<uint16_t>(buf[pos + 1]) << 8);
}

uint32_t rd32(std::span<const uint8_t> buf, std::size_t pos)
{
    return static_cast<uint32_t>(buf[pos]) | (static_cast<uint32_t>(buf[pos + 1]) << 8) |
           (static_cast<uint32_t>(buf[pos + 2]) << 16) |
           (static_cast<uint32_t>(buf[pos + 3]) << 24);
}

struct CentralDir
{
    std::size_t offset;
    uint16_t entry_count;
};

std::expected<CentralDir, ZipError> find_central_dir(std::span<const uint8_t> buf)
{
    if (buf.size() < EOCD_MIN_SIZE)
        return std::unexpected(ZipError{"buffer too small to be a ZIP archive"});

    // Scan backwards for the end-of-central-directory signature.  The record
    // closest to the end wins — a crafted comment could embed the signature
    // earlier, but scanning from the back finds the real one first.
    std::size_t scan_start = buf.size() - EOCD_MIN_SIZE;
    std::size_t scan_limit = buf.size() > EOCD_MAX_SCAN ? buf.size() - EOCD_MAX_SCAN : 0;
    for (std::size_t pos = scan_start;; --pos) {
        if (rd32(buf, pos) == EOCD_SIG) {
            uint16_t entry_count = rd16(buf, pos + 10);
            uint32_t cd_size = rd32(buf, pos + 12);
            uint32_t cd_offset = rd32(buf, pos + 16);
            if (static_cast<std::size_t>(cd_offset) + cd_size > pos)
                return std::unexpected(ZipError{"central directory extends past EOCD record"});
            return CentralDir{cd_offset, entry_count};
        }
        if (pos == scan_limit)
            break;
    }
    return std::unexpected(ZipError{"no end-of-central-directory record (not a ZIP archive)"});
}

/** Walk every central-directory entry, invoking fn(entry).  fn returns true
 *  to keep iterating, false to stop early. */
template <typename Fn>
std::expected<void, ZipError> for_each_entry(std::span<const uint8_t> buf, Fn &&fn)
{
    auto cd = find_central_dir(buf);
    if (!cd.has_value())
        return std::unexpected(cd.error());

    std::size_t pos = cd->offset;
    for (uint16_t i = 0; i < cd->entry_count; ++i) {
        if (pos + 46 > buf.size() || rd32(buf, pos) != CDIR_SIG)
            return std::unexpected(ZipError{"malformed central directory entry"});

        Entry e;
        e.method = rd16(buf, pos + 10);
        e.compressed_size = rd32(buf, pos + 20);
        e.uncompressed_size = rd32(buf, pos + 24);
        uint16_t name_len = rd16(buf, pos + 28);
        uint16_t extra_len = rd16(buf, pos + 30);
        uint16_t comment_len = rd16(buf, pos + 32);
        e.local_header_offset = rd32(buf, pos + 42);

        if (pos + 46 + name_len > buf.size())
            return std::unexpected(ZipError{"central directory entry name out of bounds"});
        e.name.assign(reinterpret_cast<const char *>(buf.data() + pos + 46), name_len);

        if (!fn(e))
            break;
        pos += 46 + name_len + extra_len + comment_len;
    }
    return {};
}

struct DecompressorDeleter
{
    void operator()(libdeflate_decompressor *d) const { libdeflate_free_decompressor(d); }
};

// libdeflate picks its CPU-specific decoder on the first decompression and
// stores the choice in plain (non-atomic) globals.  Doing one tiny
// decompression under call_once makes that write happen-before every later
// call, so concurrent read_entry() calls never race on it.
void init_libdeflate_once()
{
    static std::once_flag flag;
    std::call_once(flag, [] {
        // An empty, final, stored DEFLATE block.
        static constexpr uint8_t kEmptyStream[] = {0x01, 0x00, 0x00, 0xff, 0xff};
        std::unique_ptr<libdeflate_decompressor, DecompressorDeleter> d(
            libdeflate_alloc_decompressor());
        if (d == nullptr)
            throw std::bad_alloc();
        uint8_t out = 0;
        libdeflate_deflate_decompress(d.get(), kEmptyStream, sizeof(kEmptyStream), &out, 0,
                                      nullptr);
    });
}

std::expected<std::vector<uint8_t>, ZipError> inflate_raw(std::span<const uint8_t> compressed,
                                                          uint32_t uncompressed_size)
{
    // A DEFLATE stream cannot expand more than 1032:1, so an entry declaring an
    // uncompressed size far beyond that for its compressed payload is malformed
    // or a zip bomb — reject it before allocating (a tiny entry must not force a
    // multi-gigabyte allocation).  The +64 slack covers rounding on tiny streams.
    constexpr uint64_t MAX_DEFLATE_RATIO = 1032;
    uint64_t max_plausible = static_cast<uint64_t>(compressed.size()) * MAX_DEFLATE_RATIO + 64;
    if (uncompressed_size > max_plausible)
        return std::unexpected(ZipError{"implausible uncompressed size (zip bomb?)"});

    init_libdeflate_once();
    // One decompressor per call: libdeflate decompressors must not be shared
    // between threads, and a thread_local one would leak on foreign threads.
    std::unique_ptr<libdeflate_decompressor, DecompressorDeleter> d(
        libdeflate_alloc_decompressor());
    if (d == nullptr)
        throw std::bad_alloc();

    std::vector<uint8_t> out(uncompressed_size);
    // An empty vector's data() may be null; libdeflate is C and must not be
    // handed a null output pointer, even with zero space.
    uint8_t empty_out = 0;
    uint8_t *out_ptr = out.empty() ? &empty_out : out.data();

    // Raw DEFLATE (no zlib/gzip header), as ZIP stores it.  Passing an
    // actual-size pointer makes a short stream return SUCCESS with a smaller
    // count instead of an error, so both "too short" and "too long" (which
    // fails with INSUFFICIENT_SPACE) are caught by the check below.
    std::size_t actual = 0;
    libdeflate_result rc = libdeflate_deflate_decompress(
        d.get(), compressed.data(), compressed.size(), out_ptr, out.size(), &actual);
    if (rc != LIBDEFLATE_SUCCESS || actual != uncompressed_size)
        return std::unexpected(ZipError{"deflate stream is corrupt or size mismatch"});
    return out;
}

} // namespace

std::expected<uint32_t, ZipError> central_directory_offset(std::span<const uint8_t> buf)
{
    auto cd = find_central_dir(buf);
    if (!cd.has_value())
        return std::unexpected(cd.error());
    return static_cast<uint32_t>(cd->offset);
}

std::expected<std::vector<Entry>, ZipError> entries(std::span<const uint8_t> buf)
{
    std::vector<Entry> out;
    auto res = for_each_entry(buf, [&](const Entry &e) {
        out.push_back(e);
        return true;
    });
    if (!res.has_value())
        return std::unexpected(res.error());
    return out;
}

std::expected<std::vector<std::string>, ZipError> entry_names(std::span<const uint8_t> buf)
{
    std::vector<std::string> names;
    auto res = for_each_entry(buf, [&](const Entry &e) {
        names.push_back(e.name);
        return true;
    });
    if (!res.has_value())
        return std::unexpected(res.error());
    return names;
}

std::expected<std::vector<uint8_t>, ZipError> read_entry(std::span<const uint8_t> buf,
                                                         std::string_view name)
{
    std::optional<Entry> found;
    auto res = for_each_entry(buf, [&](const Entry &e) {
        if (e.name == name) {
            found = e;
            return false;
        }
        return true;
    });
    if (!res.has_value())
        return std::unexpected(res.error());
    if (!found.has_value())
        return std::unexpected(ZipError{"no entry named '" + std::string(name) + "'"});
    return read_entry(buf, *found);
}

std::expected<std::vector<uint8_t>, ZipError> read_entry(std::span<const uint8_t> buf,
                                                         const Entry &entry)
{
    // The local header's name/extra lengths can differ from the central
    // directory's; the data offset must be computed from the local copy.
    std::size_t lho = entry.local_header_offset;
    if (lho > buf.size() || buf.size() - lho < 30 || rd32(buf, lho) != LOCAL_SIG)
        return std::unexpected(ZipError{"malformed local file header"});
    std::size_t data_off = lho + 30 + rd16(buf, lho + 26) + rd16(buf, lho + 28);

    if (data_off > buf.size() || buf.size() - data_off < entry.compressed_size)
        return std::unexpected(ZipError{"entry data out of bounds"});
    auto data = buf.subspan(data_off, entry.compressed_size);

    switch (entry.method) {
    case 0: // stored
        if (entry.compressed_size != entry.uncompressed_size)
            return std::unexpected(ZipError{"stored entry with mismatched sizes"});
        return std::vector<uint8_t>(data.begin(), data.end());
    case 8: // deflate
        return inflate_raw(data, entry.uncompressed_size);
    default:
        return std::unexpected(
            ZipError{"unsupported compression method " + std::to_string(entry.method)});
    }
}

} // namespace dex::raw::zip
