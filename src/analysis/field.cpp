#include "field.hpp"

#include "analysis_context_impl.hpp"
#include "raw/parser.hpp"

namespace dex {

Field::Field(std::shared_ptr<const detail::AnalysisContextImpl> impl, std::size_t dex_idx,
             uint32_t field_abs_idx, uint32_t access_flags)
    : impl_(std::move(impl)), dex_idx_(dex_idx), field_abs_idx_(field_abs_idx),
      access_flags_(access_flags)
{
}

std::string_view Field::name() const
{
    const auto *field_id = impl_->field_item(dex_idx_, field_abs_idx_);
    if (!field_id)
        return {};
    return impl_->string_of(dex_idx_, field_id->name_idx);
}

ClassRef Field::declaring_class() const
{
    const auto *field_id = impl_->field_item(dex_idx_, field_abs_idx_);
    auto desc =
        field_id ? impl_->type_descriptor_of(dex_idx_, field_id->class_idx) : std::string_view{};
    return ClassRef(desc, impl_);
}

TypeDescriptor Field::type() const
{
    const auto *field_id = impl_->field_item(dex_idx_, field_abs_idx_);
    auto desc =
        field_id ? impl_->type_descriptor_of(dex_idx_, field_id->type_idx) : std::string_view{};
    return TypeDescriptor(desc);
}

std::vector<Annotation> Field::annotations() const
{
    const auto &dex = impl_->dex_files()[dex_idx_];
    const auto *fid = impl_->field_item(dex_idx_, field_abs_idx_);
    if (!fid)
        return {};

    auto def_idx = impl_->class_def_of_type(dex_idx_, fid->class_idx);
    if (!def_idx.has_value())
        return {};
    const auto &cdef = dex.class_defs()[*def_idx];
    if (cdef.annotations_off == 0)
        return {};

    auto dir =
        raw::parser::parse_annotations_directory(impl_->buffer_of(dex_idx_), cdef.annotations_off);
    if (!dir.has_value())
        return {};

    for (const auto &fa : dir->field_annotations)
        if (fa.field_idx == field_abs_idx_)
            return detail::annotations_at_set(*impl_, dex_idx_, fa.annotations_off);
    return {};
}

std::optional<AnnotationValue> Field::initial_value() const
{
    if (!is_static())
        return std::nullopt;

    const auto &dex = impl_->dex_files()[dex_idx_];
    const auto *fid = impl_->field_item(dex_idx_, field_abs_idx_);
    if (!fid)
        return std::nullopt;

    auto def_idx = impl_->class_def_of_type(dex_idx_, fid->class_idx);
    if (!def_idx.has_value())
        return std::nullopt;
    const auto &cdef = dex.class_defs()[*def_idx];
    if (cdef.static_values_off == 0)
        return std::nullopt;

    // The encoded array holds values for a PREFIX of static_fields, in
    // class_data order; fields past the prefix take their type's default.
    const auto *cd = impl_->get_class_data(dex_idx_, cdef.class_data_off);
    if (!cd)
        return std::nullopt;

    std::size_t position = cd->static_fields.size();
    for (std::size_t i = 0; i < cd->static_fields.size(); ++i) {
        if (cd->static_fields[i].field_idx == field_abs_idx_) {
            position = i;
            break;
        }
    }

    uint32_t offset = cdef.static_values_off;
    auto values = raw::parser::parse_encoded_array(impl_->buffer_of(dex_idx_), offset);
    if (!values.has_value() || position >= values->size())
        return std::nullopt;

    return detail::resolve_encoded_value(*impl_, dex_idx_, (*values)[position]);
}

} // namespace dex
