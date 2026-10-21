#include "annotations.hpp"

#include <utility>

#include "analysis_context_impl.hpp"
#include "raw/parser.hpp"

namespace dex::detail {

std::optional<uint32_t> AnalysisContextImpl::class_def_of_type(std::size_t dex_idx,
                                                               uint32_t type_idx) const
{
    const auto &defs = dex_files_vec[dex_idx].class_defs();
    for (uint32_t j = 0; j < static_cast<uint32_t>(defs.size()); ++j)
        if (defs[j].class_idx == type_idx)
            return j;
    return std::nullopt;
}

AnnotationValue resolve_encoded_value(const AnalysisContextImpl &impl, std::size_t dex_idx,
                                      const raw::EncodedValue &value)
{
    using T = raw::EncodedValue::Type;
    AnnotationValue out;

    switch (value.type) {
    case T::Byte:
    case T::Short:
    case T::Char:
    case T::Int:
    case T::Long:
        out.value = std::get<int64_t>(value.value);
        break;
    case T::Boolean:
        out.value = std::get<bool>(value.value);
        break;
    case T::Float:
    case T::Double:
        out.value = std::get<double>(value.value);
        break;
    case T::String:
        out.value = std::string(impl.string_of(dex_idx, std::get<uint32_t>(value.value)));
        break;
    case T::Type:
        out.value = AnnotationValue::TypeRef{
            std::string(impl.type_descriptor_of(dex_idx, std::get<uint32_t>(value.value)))};
        break;
    case T::Enum:
        out.value =
            AnnotationValue::EnumRef{impl.field_id_of(dex_idx, std::get<uint32_t>(value.value))};
        break;
    case T::Field:
        out.value =
            AnnotationValue::FieldRef{impl.field_id_of(dex_idx, std::get<uint32_t>(value.value))};
        break;
    case T::Method:
        out.value =
            AnnotationValue::MethodRef{impl.method_id_of(dex_idx, std::get<uint32_t>(value.value))};
        break;
    case T::MethodType:
        out.value = AnnotationValue::MethodTypeRef{
            impl.proto_descriptor_of(dex_idx, std::get<uint32_t>(value.value))};
        break;
    case T::MethodHandle:
        out.value = AnnotationValue::MethodHandleRef{std::get<uint32_t>(value.value)};
        break;
    case T::Null:
        out.value = nullptr;
        break;
    case T::Array: {
        const auto &raw_items = std::get<std::vector<raw::EncodedValue>>(value.value);
        std::vector<AnnotationValue> items;
        items.reserve(raw_items.size());
        for (const auto &item : raw_items)
            items.push_back(resolve_encoded_value(impl, dex_idx, item));
        out.value = std::move(items);
        break;
    }
    case T::Annotation: {
        const auto &nested = *std::get<std::unique_ptr<raw::EncodedAnnotation>>(value.value);
        // Nested annotations carry no visibility byte of their own; Runtime is
        // the only visibility under which a consumer can observe them.
        out.value = std::make_shared<const Annotation>(
            resolve_annotation(impl, dex_idx, nested, AnnotationVisibility::Runtime));
        break;
    }
    }
    return out;
}

Annotation resolve_annotation(const AnalysisContextImpl &impl, std::size_t dex_idx,
                              const raw::EncodedAnnotation &ann, AnnotationVisibility visibility)
{
    Annotation out;
    out.type_descriptor = std::string(impl.type_descriptor_of(dex_idx, ann.type_idx));
    out.visibility = visibility;
    out.elements.reserve(ann.elements.size());
    for (const auto &e : ann.elements) {
        out.elements.push_back(AnnotationElement{
            std::string(impl.string_of(dex_idx, e.name_idx)),
            resolve_encoded_value(impl, dex_idx, e.value),
        });
    }
    return out;
}

std::vector<Annotation> annotations_at_set(const AnalysisContextImpl &impl, std::size_t dex_idx,
                                           uint32_t set_off)
{
    if (set_off == 0)
        return {};

    auto buf = impl.buffer_of(dex_idx);
    auto set = raw::parser::parse_annotation_set(buf, set_off);
    if (!set.has_value())
        return {};

    std::vector<Annotation> out;
    out.reserve(set->entries.size());
    for (uint32_t ann_off : set->entries) {
        auto item = raw::parser::parse_annotation_item(buf, ann_off);
        if (!item.has_value())
            continue; // skip malformed entries rather than dropping the whole set
        out.push_back(resolve_annotation(impl, dex_idx, item->annotation,
                                         static_cast<AnnotationVisibility>(item->visibility)));
    }
    return out;
}

} // namespace dex::detail
