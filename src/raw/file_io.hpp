#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "common/error.hpp"

namespace dex::raw {

/** Read a whole file into memory.
 *
 *  Every loader of the library reads its input through this function, so they
 *  all treat a bad path the same way.  It never throws for a path it cannot
 *  read, and it never trusts a size it did not read: a directory, a missing
 *  file and a file without read permission each give an Error: FileNotFound
 *  for a path that does not exist, CannotRead for every other failure.  The
 *  message names the path and the reason. */
[[nodiscard]] std::expected<std::vector<uint8_t>, Error> read_whole_file(const std::string &path);

/** Create or replace the file @p path with @p bytes.
 *
 *  Success means every byte reached the operating system: the result of
 *  closing the file is checked too, because a write that does not fit on the
 *  disk often fails only when the last buffer is flushed.  On failure the
 *  error code is CannotWrite, and the file may be left partly written. */
[[nodiscard]] std::expected<void, Error> write_whole_file(const std::string &path,
                                                          std::span<const uint8_t> bytes);

} // namespace dex::raw
