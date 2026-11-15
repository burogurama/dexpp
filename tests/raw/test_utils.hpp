#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

inline std::vector<uint8_t> load_buf(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        throw std::runtime_error("Cannot open: " + path);
    f.seekg(0, std::ios::end);
    auto size = static_cast<std::size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(size);
    f.read(reinterpret_cast<char *>(buf.data()), size);
    return buf;
}

/** True when @p path names a fixture in tests/data/local/ that is not there.
 *
 *  tests/data/local/ holds real apps for extra coverage.  They are not part of
 *  the repository, so a checkout does not have them.  A test that uses one
 *  asks this first and leaves that file out (or skips itself) when it is
 *  missing.  A missing fixture anywhere else is still an error. */
inline bool missing_local_fixture(const std::string &path)
{
    return path.starts_with("tests/data/local/") && !std::filesystem::exists(path);
}
