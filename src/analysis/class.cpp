#include "class.hpp"

#include <span>

#include "analysis_context_impl.hpp"
#include "raw/parser.hpp"

namespace dex {

static constexpr uint32_t NO_INDEX = 0xFFFFFFFF;

Class::Class(std::shared_ptr<const detail::AnalysisContextImpl> impl, std::size_t dex_idx,
             uint32_t class_def_idx)
    : impl_(std::move(impl)), dex_idx_(dex_idx), class_def_idx_(class_def_idx)
{
}

std::string_view Class::name() const
{
    const auto &cdef = impl_->dex_files()[dex_idx_].class_defs()[class_def_idx_];
    return impl_->type_descriptor_of(dex_idx_, cdef.class_idx);
}

std::string_view Class::package() const
{
    auto n = name();
    // Strip 'L' prefix and ';' suffix to get the internal form, then find the last '/'.
    if (n.size() < 2 || n.front() != 'L' || n.back() != ';')
        return {};
    auto inner = n.substr(1, n.size() - 2);
    auto last_slash = inner.rfind('/');
    if (last_slash == std::string_view::npos)
        return {};
    return inner.substr(0, last_slash);
}

std::optional<ClassRef> Class::superclass() const
{
    const auto &cdef = impl_->dex_files()[dex_idx_].class_defs()[class_def_idx_];
    if (cdef.superclass_idx == NO_INDEX)
        return std::nullopt;
    auto desc = impl_->type_descriptor_of(dex_idx_, cdef.superclass_idx);
    return ClassRef(desc, impl_);
}

std::vector<ClassRef> Class::interfaces() const
{
    const auto &cdef = impl_->dex_files()[dex_idx_].class_defs()[class_def_idx_];
    if (cdef.interfaces_off == 0)
        return {};

    auto type_list = raw::parser::parse_type_list(impl_->buffer_of(dex_idx_), cdef.interfaces_off);
    if (!type_list.has_value())
        return {};

    std::vector<ClassRef> refs;
    refs.reserve(type_list->type_ids.size());
    for (auto type_idx : type_list->type_ids) {
        auto desc = impl_->type_descriptor_of(dex_idx_, type_idx);
        refs.emplace_back(desc, impl_);
    }
    return refs;
}

std::vector<Method> Class::methods() const
{
    const auto &cdef = impl_->dex_files()[dex_idx_].class_defs()[class_def_idx_];
    const auto *cd = impl_->get_class_data(dex_idx_, cdef.class_data_off);
    if (!cd)
        return {};

    std::vector<Method> result;
    result.reserve(cd->direct_methods.size() + cd->virtual_methods.size());

    for (const auto &em : cd->direct_methods) {
        result.emplace_back(impl_, dex_idx_, em.method_idx, em.access_flags, em.code_off);
    }
    for (const auto &em : cd->virtual_methods) {
        result.emplace_back(impl_, dex_idx_, em.method_idx, em.access_flags, em.code_off);
    }
    return result;
}

std::vector<Field> Class::fields() const
{
    const auto &cdef = impl_->dex_files()[dex_idx_].class_defs()[class_def_idx_];
    const auto *cd = impl_->get_class_data(dex_idx_, cdef.class_data_off);
    if (!cd)
        return {};

    std::vector<Field> result;
    result.reserve(cd->static_fields.size() + cd->instance_fields.size());

    for (const auto &ef : cd->static_fields) {
        result.emplace_back(impl_, dex_idx_, ef.field_idx, ef.access_flags);
    }
    for (const auto &ef : cd->instance_fields) {
        result.emplace_back(impl_, dex_idx_, ef.field_idx, ef.access_flags);
    }
    return result;
}

std::vector<Annotation> Class::annotations() const
{
    const auto &cdef = impl_->dex_files()[dex_idx_].class_defs()[class_def_idx_];
    if (cdef.annotations_off == 0)
        return {};

    auto dir =
        raw::parser::parse_annotations_directory(impl_->buffer_of(dex_idx_), cdef.annotations_off);
    if (!dir.has_value())
        return {};
    return detail::annotations_at_set(*impl_, dex_idx_, dir->class_annotations_off);
}

AccessFlags Class::access_flags() const
{
    const auto &cdef = impl_->dex_files()[dex_idx_].class_defs()[class_def_idx_];
    return static_cast<AccessFlags>(cdef.access_flags);
}

std::optional<std::string_view> Class::source_file() const
{
    const auto &cdef = impl_->dex_files()[dex_idx_].class_defs()[class_def_idx_];
    if (cdef.source_file_idx == NO_INDEX)
        return std::nullopt;
    return impl_->string_of(dex_idx_, cdef.source_file_idx);
}

} // namespace dex
