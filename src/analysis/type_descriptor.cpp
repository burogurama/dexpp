#include "type_descriptor.hpp"

#include "analysis_context.hpp"

namespace dex {

TypeDescriptor::TypeDescriptor(std::string_view descriptor) : descriptor_(descriptor) {}

bool TypeDescriptor::is_primitive() const
{
    if (descriptor_.size() != 1)
        return false;
    switch (descriptor_.front()) {
    case 'V':
    case 'B':
    case 'C':
    case 'D':
    case 'F':
    case 'I':
    case 'J':
    case 'S':
    case 'Z':
        return true;
    default:
        return false;
    }
}

std::optional<ClassRef> TypeDescriptor::as_class_ref(const AnalysisContext *ctx) const
{
    if (!is_class())
        return std::nullopt;
    return ClassRef(descriptor_, ctx);
}

std::optional<TypeDescriptor> TypeDescriptor::element_type() const
{
    if (!is_array())
        return std::nullopt;
    return TypeDescriptor(descriptor_.substr(1));
}

} // namespace dex
