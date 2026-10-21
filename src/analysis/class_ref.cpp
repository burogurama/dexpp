#include "class_ref.hpp"

#include "analysis_context.hpp"
#include "analysis_context_impl.hpp"
#include "class.hpp"

namespace dex {

ClassRef::ClassRef(std::string_view descriptor, const AnalysisContext *ctx)
    : descriptor_(descriptor), impl_(ctx ? ctx->impl_ : nullptr)
{
}

ClassRef::ClassRef(std::string_view descriptor,
                   std::shared_ptr<const detail::AnalysisContextImpl> impl)
    : descriptor_(descriptor), impl_(std::move(impl))
{
}

Class ClassRef::resolve() const
{
    if (!impl_)
        return {};
    auto idx = impl_->find_class_index(descriptor_);
    if (!idx.has_value())
        return {};
    return Class(impl_, idx->first, idx->second);
}

bool ClassRef::is_resolved() const
{
    if (!impl_)
        return false;
    return impl_->find_class_index(descriptor_).has_value();
}

} // namespace dex
