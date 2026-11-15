#pragma once

#include <cstddef>

namespace dex::detail {

/** Mix @p value into @p seed, to hash a value made of several parts (the
 *  boost-style combiner).  Internal: included only by .cpp files. */
inline void hash_combine(std::size_t &seed, std::size_t value) noexcept
{
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}

} // namespace dex::detail
