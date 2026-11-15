#include "class_ref.hpp"

#include "analysis_context_impl.hpp"
#include "class.hpp"
#include "common/hash.hpp"

namespace dex {

ClassRef::ClassRef(std::string_view descriptor,
                   std::shared_ptr<const detail::AnalysisContextImpl> impl)
    : descriptor_(descriptor), impl_(std::move(impl))
{
}

std::optional<Class> ClassRef::resolve() const
{
    auto idx = impl_->find_class_index(descriptor_);
    if (!idx.has_value())
        return std::nullopt;
    return Class(impl_, idx->first, idx->second);
}

bool ClassRef::is_resolved() const { return impl_->find_class_index(descriptor_).has_value(); }

} // namespace dex

std::size_t std::hash<dex::ClassRef>::operator()(const dex::ClassRef &ref) const noexcept
{
    std::size_t h = std::hash<const void *>{}(ref.impl_.get());
    dex::detail::hash_combine(h, std::hash<std::string>{}(ref.descriptor_));
    return h;
}
