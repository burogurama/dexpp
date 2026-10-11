#include "method.hpp"

#include <algorithm>
#include <type_traits>
#include <variant>

#include "analysis_context_impl.hpp"
#include "raw/parser.hpp"

namespace dex {

Method::Method(std::shared_ptr<const detail::AnalysisContextImpl> impl, std::size_t dex_idx,
               uint32_t method_abs_idx, uint32_t access_flags, uint32_t code_off)
    : impl_(std::move(impl)), dex_idx_(dex_idx), method_abs_idx_(method_abs_idx),
      access_flags_(access_flags), code_off_(code_off)
{
}

std::string_view Method::name() const
{
    const auto *method_id = impl_->method_item(dex_idx_, method_abs_idx_);
    if (!method_id)
        return {};
    return impl_->string_of(dex_idx_, method_id->name_idx);
}

ClassRef Method::declaring_class() const
{
    const auto *method_id = impl_->method_item(dex_idx_, method_abs_idx_);
    auto desc =
        method_id ? impl_->type_descriptor_of(dex_idx_, method_id->class_idx) : std::string_view{};
    return ClassRef(desc, impl_);
}

TypeDescriptor Method::return_type() const
{
    const auto *method_id = impl_->method_item(dex_idx_, method_abs_idx_);
    const auto *proto = method_id ? impl_->proto_item(dex_idx_, method_id->proto_idx) : nullptr;
    auto desc =
        proto ? impl_->type_descriptor_of(dex_idx_, proto->return_type_idx) : std::string_view{};
    return TypeDescriptor(desc);
}

std::vector<TypeDescriptor> Method::parameters() const
{
    const auto *method_id = impl_->method_item(dex_idx_, method_abs_idx_);
    const auto *proto = method_id ? impl_->proto_item(dex_idx_, method_id->proto_idx) : nullptr;
    if (!proto || proto->parameters_off == 0)
        return {};

    auto type_list =
        raw::parser::parse_type_list(impl_->buffer_of(dex_idx_), proto->parameters_off);
    if (!type_list.has_value())
        return {};

    std::vector<TypeDescriptor> params;
    params.reserve(type_list->type_ids.size());
    for (auto type_idx : type_list->type_ids) {
        params.emplace_back(impl_->type_descriptor_of(dex_idx_, type_idx));
    }
    return params;
}

bool Method::is_constructor() const
{
    auto n = name();
    return n == "<init>" || n == "<clinit>";
}

std::span<const Instruction> Method::instructions() const
{
    return impl_->get_instructions(dex_idx_, code_off_);
}

const Cfg &Method::cfg() const { return impl_->get_cfg(dex_idx_, code_off_); }

uint16_t Method::register_count() const
{
    const auto *code = impl_->get_code_item(dex_idx_, code_off_);
    return code ? code->registers_size : 0;
}

std::vector<ParameterRegister> Method::parameter_registers() const
{
    const auto *code = impl_->get_code_item(dex_idx_, code_off_);
    // No code item (abstract/native), or a malformed frame where the incoming
    // window doesn't fit: no register mapping to report.
    if (!code || code->ins_size > code->registers_size)
        return {};

    std::vector<ParameterRegister> out;
    uint16_t reg = static_cast<uint16_t>(code->registers_size - code->ins_size);

    if (!is_static()) {
        // The implicit receiver occupies the first incoming register.  Take its
        // type from the stable string cache, not a temporary ClassRef.
        const auto *mid = impl_->method_item(dex_idx_, method_abs_idx_);
        auto this_desc =
            mid ? impl_->type_descriptor_of(dex_idx_, mid->class_idx) : std::string_view{};
        out.push_back({reg, TypeDescriptor(this_desc), /*is_this=*/true, /*is_wide=*/false});
        ++reg;
    }

    for (const auto &param : parameters()) {
        auto desc = param.descriptor();
        bool wide = desc == "J" || desc == "D";
        out.push_back({reg, param, /*is_this=*/false, wide});
        reg = static_cast<uint16_t>(reg + (wide ? 2 : 1));
    }
    return out;
}

std::vector<MethodCall> Method::calls() const
{
    std::vector<MethodCall> out;
    for (const auto &insn : instructions()) {
        if (const auto *iv = std::get_if<InvokeInstruction>(&insn)) {
            if (auto kind = invoke_kind_from_opcode(iv->base.opcode))
                out.push_back(
                    {iv->base.offset, impl_->method_id_of(dex_idx_, iv->method_index), *kind});
        }
    }
    return out;
}

std::vector<FieldAccess> Method::field_accesses() const
{
    std::vector<FieldAccess> out;
    for (const auto &insn : instructions()) {
        if (const auto *f = std::get_if<FieldInstruction>(&insn))
            out.push_back({f->base.offset, impl_->field_id_of(dex_idx_, f->field_index),
                           f->is_write, f->is_static});
    }
    return out;
}

std::vector<StringLoad> Method::string_loads() const
{
    std::vector<StringLoad> out;
    for (const auto &insn : instructions()) {
        if (const auto *s = std::get_if<ConstStringInstruction>(&insn))
            out.push_back({s->base.offset, impl_->string_of(dex_idx_, s->string_index)});
    }
    return out;
}

std::vector<TypeUse> Method::type_uses() const
{
    std::vector<TypeUse> out;
    for (const auto &insn : instructions()) {
        std::visit(
            [&](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, ConstClassInstruction>) {
                    out.push_back({v.base.offset, impl_->type_descriptor_of(dex_idx_, v.type_index),
                                   TypeRefKind::ConstClass});
                }
                else if constexpr (std::is_same_v<T, TypeInstruction>) {
                    if (auto kind = type_ref_kind_from_opcode(v.base.opcode))
                        out.push_back({v.base.offset,
                                       impl_->type_descriptor_of(dex_idx_, v.type_index), *kind});
                }
                else if constexpr (std::is_same_v<T, FilledNewArrayInstruction>) {
                    out.push_back({v.base.offset, impl_->type_descriptor_of(dex_idx_, v.type_index),
                                   TypeRefKind::FilledNewArray});
                }
            },
            insn);
    }
    return out;
}

std::vector<Annotation> Method::annotations() const
{
    const auto &dex = impl_->dex_files()[dex_idx_];
    const auto *mid = impl_->method_item(dex_idx_, method_abs_idx_);
    if (!mid)
        return {};

    auto def_idx = impl_->class_def_of_type(dex_idx_, mid->class_idx);
    if (!def_idx.has_value())
        return {};
    const auto &cdef = dex.class_defs()[*def_idx];
    if (cdef.annotations_off == 0)
        return {};

    auto dir =
        raw::parser::parse_annotations_directory(impl_->buffer_of(dex_idx_), cdef.annotations_off);
    if (!dir.has_value())
        return {};

    for (const auto &ma : dir->method_annotations)
        if (ma.method_idx == method_abs_idx_)
            return detail::annotations_at_set(*impl_, dex_idx_, ma.annotations_off);
    return {};
}

std::vector<TryBlock> Method::try_blocks() const
{
    const auto *code = impl_->get_code_item(dex_idx_, code_off_);
    if (!code)
        return {};

    std::vector<TryBlock> out;
    out.reserve(code->tries.size());
    for (const auto &t : code->tries) {
        TryBlock tb;
        tb.start_offset = t.start_addr;
        tb.end_offset = t.start_addr + t.insn_count;

        // Resolve handler_off → handlers index via handler_byte_offsets.
        auto hit = std::find(code->handler_byte_offsets.begin(), code->handler_byte_offsets.end(),
                             t.handler_off);
        if (hit != code->handler_byte_offsets.end()) {
            std::size_t hi =
                static_cast<std::size_t>(std::distance(code->handler_byte_offsets.begin(), hit));
            if (hi < code->handlers.size()) {
                const auto &h = code->handlers[hi];
                tb.handlers.reserve(h.handlers.size());
                for (const auto &p : h.handlers)
                    tb.handlers.push_back(
                        {impl_->type_descriptor_of(dex_idx_, p.type_idx), p.addr});
                tb.catch_all_offset = h.catch_all_addr;
            }
        }
        out.push_back(std::move(tb));
    }
    return out;
}

} // namespace dex
