#include "xrefs.hpp"

#include <type_traits>
#include <variant>

#include "analysis_context_impl.hpp"
#include "instruction.hpp"
#include "method.hpp"

namespace dex {

namespace {

// Standard hash combiner (boost-style xor-shift mixer).
inline void hash_combine(std::size_t &seed, std::size_t v) noexcept
{
    seed ^= v + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}

} // namespace

std::optional<TypeRefKind> type_ref_kind_from_opcode(uint8_t opcode)
{
    switch (opcode) {
    case 0x1c:
        return TypeRefKind::ConstClass;
    case 0x1f:
        return TypeRefKind::CheckCast;
    case 0x20:
        return TypeRefKind::InstanceOf;
    case 0x22:
        return TypeRefKind::NewInstance;
    case 0x23:
        return TypeRefKind::NewArray;
    case 0x24:
    case 0x25:
        return TypeRefKind::FilledNewArray;
    default:
        return std::nullopt;
    }
}

std::size_t FieldIdHash::operator()(const FieldId &f) const noexcept
{
    std::size_t h = std::hash<std::string>{}(f.class_descriptor);
    hash_combine(h, std::hash<std::string>{}(f.name));
    hash_combine(h, std::hash<std::string>{}(f.type));
    return h;
}

std::optional<Method> Xrefs::referrer_method(uint32_t referrer) const
{
    if (referrer >= referrer_refs_.size())
        return std::nullopt;

    auto impl = impl_.lock();
    if (!impl)
        return std::nullopt;

    const auto &ref = referrer_refs_[referrer];
    return Method(std::move(impl), ref.dex_idx, ref.method_abs_idx, ref.access_flags, ref.code_off);
}

std::span<const MethodXref> Xrefs::method_refs(const MethodId &id) const
{
    auto it = method_map_.find(id);
    if (it == method_map_.end())
        return {};
    return it->second;
}

std::vector<std::pair<const MethodId *, std::span<const MethodXref>>>
Xrefs::method_refs_by_name(std::string_view name) const
{
    std::vector<std::pair<const MethodId *, std::span<const MethodXref>>> out;
    auto it = name_index_.find(name);
    if (it == name_index_.end())
        return out;
    out.reserve(it->second.size());
    for (const auto *entry : it->second)
        out.emplace_back(&entry->first, std::span<const MethodXref>(entry->second));
    return out;
}

std::span<const FieldXref> Xrefs::field_refs(const FieldId &id) const
{
    auto it = field_map_.find(id);
    if (it == field_map_.end())
        return {};
    return it->second;
}

std::vector<FieldXref> Xrefs::field_reads(const FieldId &id) const
{
    std::vector<FieldXref> out;
    for (const auto &x : field_refs(id))
        if (!x.is_write)
            out.push_back(x);
    return out;
}

std::vector<FieldXref> Xrefs::field_writes(const FieldId &id) const
{
    std::vector<FieldXref> out;
    for (const auto &x : field_refs(id))
        if (x.is_write)
            out.push_back(x);
    return out;
}

std::span<const StringXref> Xrefs::string_refs(std::string_view value) const
{
    auto it = string_map_.find(value);
    if (it == string_map_.end())
        return {};
    return it->second;
}

std::vector<std::string_view> Xrefs::referenced_strings() const
{
    std::vector<std::string_view> out;
    out.reserve(string_map_.size());
    for (const auto &[value, sites] : string_map_)
        out.emplace_back(value);
    return out;
}

std::span<const TypeXref> Xrefs::type_refs(std::string_view descriptor) const
{
    auto it = type_map_.find(descriptor);
    if (it == type_map_.end())
        return {};
    return it->second;
}

Xrefs build_xrefs(std::shared_ptr<const detail::AnalysisContextImpl> impl)
{
    Xrefs x;
    x.impl_ = impl;

    if (!impl)
        return x;

    for (std::size_t i = 0; i < impl->dex_files().size(); ++i) {
        const auto &dex = impl->dex_files()[i];
        for (uint32_t j = 0; j < static_cast<uint32_t>(dex.class_defs().size()); ++j) {
            const auto *cd = impl->get_class_data(i, dex.class_defs()[j].class_data_off);
            if (!cd)
                continue;

            auto index_method = [&](const raw::EncodedMethod &em) {
                if (em.code_off == 0)
                    return;
                auto insns = impl->get_instructions(i, em.code_off);
                if (insns.empty())
                    return;

                // Register the referrer lazily — only methods that actually
                // contain an indexed instruction get a slot.
                uint32_t referrer = UINT32_MAX;
                auto referrer_idx = [&]() -> uint32_t {
                    if (referrer == UINT32_MAX) {
                        referrer = static_cast<uint32_t>(x.referrer_ids_.size());
                        x.referrer_ids_.push_back(impl->method_id_of(i, em.method_idx));
                        x.referrer_refs_.push_back(
                            Xrefs::ReferrerRef{i, em.method_idx, em.access_flags, em.code_off});
                    }
                    return referrer;
                };

                for (const auto &insn : insns) {
                    std::visit(
                        [&](const auto &v) {
                            using T = std::decay_t<decltype(v)>;
                            if constexpr (std::is_same_v<T, ConstStringInstruction>) {
                                std::string value(impl->string_of(i, v.string_index));
                                x.string_map_[std::move(value)].push_back(
                                    StringXref{referrer_idx(), v.base.offset});
                            }
                            else if constexpr (std::is_same_v<T, ConstClassInstruction>) {
                                std::string desc(impl->type_descriptor_of(i, v.type_index));
                                x.type_map_[std::move(desc)].push_back(TypeXref{
                                    referrer_idx(), v.base.offset, TypeRefKind::ConstClass});
                            }
                            else if constexpr (std::is_same_v<T, TypeInstruction>) {
                                auto kind = type_ref_kind_from_opcode(v.base.opcode);
                                if (!kind.has_value())
                                    return;
                                std::string desc(impl->type_descriptor_of(i, v.type_index));
                                x.type_map_[std::move(desc)].push_back(
                                    TypeXref{referrer_idx(), v.base.offset, *kind});
                            }
                            else if constexpr (std::is_same_v<T, FilledNewArrayInstruction>) {
                                std::string desc(impl->type_descriptor_of(i, v.type_index));
                                x.type_map_[std::move(desc)].push_back(TypeXref{
                                    referrer_idx(), v.base.offset, TypeRefKind::FilledNewArray});
                            }
                            else if constexpr (std::is_same_v<T, FieldInstruction>) {
                                x.field_map_[impl->field_id_of(i, v.field_index)].push_back(
                                    FieldXref{referrer_idx(), v.base.offset, v.is_write,
                                              v.is_static});
                            }
                            else if constexpr (std::is_same_v<T, InvokeInstruction>) {
                                auto kind = invoke_kind_from_opcode(v.base.opcode);
                                if (!kind.has_value())
                                    return;
                                x.method_map_[impl->method_id_of(i, v.method_index)].push_back(
                                    MethodXref{referrer_idx(), v.base.offset, *kind});
                            }
                        },
                        insn);
                }
            };

            for (const auto &em : cd->direct_methods)
                index_method(em);
            for (const auto &em : cd->virtual_methods)
                index_method(em);
        }
    }

    // Secondary index: bare method name → map entries.  Built after the walk
    // so every key's address is final.
    for (const auto &entry : x.method_map_)
        x.name_index_[entry.first.name].push_back(&entry);

    return x;
}

} // namespace dex
