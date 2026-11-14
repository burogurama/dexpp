#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace dex::raw {

/** Why read_whole_file() failed. */
struct FileError
{
    enum class Kind : uint8_t {
        NotFound,   ///< The path does not exist.
        CannotRead, ///< The path exists but cannot be read: a directory, no permission, an I/O
                    ///< error.
    };
    Kind kind;
    std::string message; ///< Names the path and the reason.
};

/** Read a whole file into memory.
 *
 *  Every loader of the library reads its input through this function, so they
 *  all treat a bad path the same way.  It never throws for a path it cannot
 *  read, and it never trusts a size it did not read: a directory, a missing
 *  file and a file without read permission each give a FileError. */
[[nodiscard]] std::expected<std::vector<uint8_t>, FileError>
read_whole_file(const std::string &path);

} // namespace dex::raw
