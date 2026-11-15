#include "raw/file_io.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <system_error>

namespace dex::raw {

namespace {

struct FileCloser
{
    void operator()(std::FILE *file) const noexcept { std::fclose(file); }
};

std::unexpected<Error> cannot_write(const std::string &path, const char *reason)
{
    return std::unexpected(
        Error{ErrorCode::CannotWrite, "Cannot write file: " + path + ": " + reason});
}

std::unexpected<Error> cannot_read(const std::string &path, const char *reason)
{
    return std::unexpected(
        Error{ErrorCode::CannotRead, "Cannot read file: " + path + ": " + reason});
}

} // namespace

std::expected<std::vector<uint8_t>, Error> read_whole_file(const std::string &path)
{
    // A directory can be opened for reading on Linux, and only the first read
    // fails.  Say what is wrong up front.
    std::error_code status_error;
    if (std::filesystem::is_directory(path, status_error))
        return cannot_read(path, "it is a directory");

    // C stdio, not std::ifstream: a failed read on a file stream throws
    // std::ios_base::failure from libstdc++, whatever its exception mask says.
    errno = 0;
    std::unique_ptr<std::FILE, FileCloser> file(std::fopen(path.c_str(), "rb"));
    if (file == nullptr) {
        if (errno == ENOENT || errno == ENOTDIR)
            return std::unexpected(Error{ErrorCode::FileNotFound, "Cannot open file: " + path});
        return cannot_read(path, std::strerror(errno));
    }

    // The reported size is only a hint for the first allocation.  The loop
    // below reads until the end of the file, whatever the size said.
    std::vector<uint8_t> bytes;
    std::error_code size_error;
    const std::uintmax_t size_hint = std::filesystem::file_size(path, size_error);
    std::size_t used = 0;
    if (!size_error && size_hint > 0 && size_hint <= bytes.max_size()) {
        bytes.resize(static_cast<std::size_t>(size_hint));
        used = std::fread(bytes.data(), 1, bytes.size(), file.get());
    }
    constexpr std::size_t kChunk = std::size_t{1} << 16;
    while (!std::ferror(file.get()) && !std::feof(file.get())) {
        if (bytes.size() - used < kChunk)
            bytes.resize(used + kChunk);
        used += std::fread(bytes.data() + used, 1, bytes.size() - used, file.get());
    }
    if (std::ferror(file.get()))
        return cannot_read(path, "read error");
    bytes.resize(used);
    return bytes;
}

std::expected<void, Error> write_whole_file(const std::string &path, std::span<const uint8_t> bytes)
{
    errno = 0;
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr)
        return cannot_write(path, std::strerror(errno));

    const bool written =
        bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    const int write_errno = errno;
    // fclose flushes the last buffer, and that is where a full disk usually
    // shows up, so its result is checked too.
    errno = 0;
    const bool closed = std::fclose(file) == 0;
    if (!written)
        return cannot_write(path, std::strerror(write_errno != 0 ? write_errno : EIO));
    if (!closed)
        return cannot_write(path, std::strerror(errno != 0 ? errno : EIO));
    return {};
}

} // namespace dex::raw
