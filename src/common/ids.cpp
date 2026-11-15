#include "common/ids.hpp"

#include <functional>

#include "common/hash.hpp"

namespace dex {

using detail::hash_combine;

std::size_t MethodIdHash::operator()(const MethodId &m) const noexcept
{
    std::size_t h = std::hash<std::string>{}(m.class_descriptor);
    hash_combine(h, std::hash<std::string>{}(m.name));
    hash_combine(h, std::hash<std::string>{}(m.proto));
    return h;
}

std::size_t FieldIdHash::operator()(const FieldId &f) const noexcept
{
    std::size_t h = std::hash<std::string>{}(f.class_descriptor);
    hash_combine(h, std::hash<std::string>{}(f.name));
    hash_combine(h, std::hash<std::string>{}(f.type));
    return h;
}

} // namespace dex
