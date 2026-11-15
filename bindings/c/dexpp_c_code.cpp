/* C ABI over the dexpp C++ API: a method's bytecode, control-flow graph, try
 * blocks, parameter registers and references, and annotation values.
 * Compiled as C++23. */

#include "dexpp.h"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "dexpp_c_internal.hpp"

namespace dexpp_c {

namespace {

// ===== instructions: C++ -> C ================================================

template <typename T> inline constexpr bool always_false = false;

void fill_regs(DexppInstruction &out, const std::vector<uint16_t> &regs)
{
    out.arg_count = regs.size();
    out.arg_regs = alloc_array<uint16_t>(regs.size());
    for (std::size_t i = 0; i < regs.size(); ++i)
        out.arg_regs[i] = regs[i];
}

DexppInstruction instruction(const dex::Instruction &insn)
{
    DexppInstruction out{};
    const dex::InstructionBase &base = dex::base_of(insn);
    out.offset = base.offset;
    out.opcode = base.opcode;
    out.size = base.size;
    std::visit(
        [&](const auto &i) {
            using T = std::decay_t<decltype(i)>;
            if constexpr (std::is_same_v<T, dex::NopInstruction>) {
                out.kind = DEXPP_INSN_NOP;
            }
            else if constexpr (std::is_same_v<T, dex::GotoInstruction>) {
                out.kind = DEXPP_INSN_GOTO;
                out.branch_offset = i.branch_offset;
            }
            else if constexpr (std::is_same_v<T, dex::IfInstruction>) {
                out.kind = DEXPP_INSN_IF;
                out.branch_offset = i.branch_offset;
                out.reg_a = i.reg_a;
                out.reg_b = i.reg_b;
            }
            else if constexpr (std::is_same_v<T, dex::IfZeroInstruction>) {
                out.kind = DEXPP_INSN_IF_ZERO;
                out.branch_offset = i.branch_offset;
                out.reg = i.reg;
            }
            else if constexpr (std::is_same_v<T, dex::SwitchInstruction>) {
                out.kind = DEXPP_INSN_SWITCH;
                out.payload_offset = i.payload_offset;
                out.reg = i.reg;
                out.is_packed = i.is_packed ? 1 : 0;
                out.case_count = i.cases.size();
                out.cases = alloc_array<DexppSwitchCase>(i.cases.size());
                for (std::size_t c = 0; c < i.cases.size(); ++c)
                    out.cases[c] = DexppSwitchCase{i.cases[c].key, i.cases[c].target};
            }
            else if constexpr (std::is_same_v<T, dex::ReturnInstruction>) {
                out.kind = DEXPP_INSN_RETURN;
                out.has_reg = i.reg.has_value() ? 1 : 0;
                out.reg = i.reg.value_or(0);
                out.width = operand_width(i.width);
            }
            else if constexpr (std::is_same_v<T, dex::ThrowInstruction>) {
                out.kind = DEXPP_INSN_THROW;
                out.reg = i.reg;
            }
            else if constexpr (std::is_same_v<T, dex::MoveInstruction>) {
                out.kind = DEXPP_INSN_MOVE;
                out.dest = i.dest;
                out.src = i.src;
                out.width = operand_width(i.width);
            }
            else if constexpr (std::is_same_v<T, dex::ConstInstruction>) {
                out.kind = DEXPP_INSN_CONST;
                out.dest = i.dest;
                out.value = i.value;
                out.width = operand_width(i.width);
            }
            else if constexpr (std::is_same_v<T, dex::ConstStringInstruction>) {
                out.kind = DEXPP_INSN_CONST_STRING;
                out.dest = i.dest;
                out.string_index = i.string_index;
            }
            else if constexpr (std::is_same_v<T, dex::ConstClassInstruction>) {
                out.kind = DEXPP_INSN_CONST_CLASS;
                out.dest = i.dest;
                out.type_index = i.type_index;
            }
            else if constexpr (std::is_same_v<T, dex::ConstHandleInstruction>) {
                out.kind = DEXPP_INSN_CONST_HANDLE;
                out.dest = i.dest;
                out.index = i.index;
            }
            else if constexpr (std::is_same_v<T, dex::TypeInstruction>) {
                out.kind = DEXPP_INSN_TYPE;
                out.type_index = i.type_index;
                out.dest = i.dest;
                out.has_src = i.src.has_value() ? 1 : 0;
                out.src = i.src.value_or(0);
            }
            else if constexpr (std::is_same_v<T, dex::FilledNewArrayInstruction>) {
                out.kind = DEXPP_INSN_FILLED_NEW_ARRAY;
                out.type_index = i.type_index;
                fill_regs(out, i.arg_regs);
            }
            else if constexpr (std::is_same_v<T, dex::MonitorInstruction>) {
                out.kind = DEXPP_INSN_MONITOR;
                out.reg = i.reg;
                out.is_enter = i.is_enter ? 1 : 0;
            }
            else if constexpr (std::is_same_v<T, dex::FillArrayInstruction>) {
                out.kind = DEXPP_INSN_FILL_ARRAY;
                out.reg = i.reg;
                out.payload_offset = i.payload_offset;
            }
            else if constexpr (std::is_same_v<T, dex::CompareInstruction>) {
                out.kind = DEXPP_INSN_COMPARE;
                out.dest = i.dest;
                out.src_a = i.src_a;
                out.src_b = i.src_b;
                out.src_width = operand_width(i.src_width);
            }
            else if constexpr (std::is_same_v<T, dex::ArrayOpInstruction>) {
                out.kind = DEXPP_INSN_ARRAY_OP;
                out.value_reg = i.value_reg;
                out.array_reg = i.array_reg;
                out.index_reg = i.index_reg;
                out.is_write = i.is_write ? 1 : 0;
                out.width = operand_width(i.width);
            }
            else if constexpr (std::is_same_v<T, dex::FieldInstruction>) {
                out.kind = DEXPP_INSN_FIELD;
                out.field_index = i.field_index;
                out.value_reg = i.value_reg;
                out.has_object_reg = i.object_reg.has_value() ? 1 : 0;
                out.object_reg = i.object_reg.value_or(0);
                out.is_static = i.is_static ? 1 : 0;
                out.is_write = i.is_write ? 1 : 0;
                out.width = operand_width(i.width);
            }
            else if constexpr (std::is_same_v<T, dex::InvokeInstruction>) {
                out.kind = DEXPP_INSN_INVOKE;
                out.method_index = i.method_index;
                fill_regs(out, i.arg_regs);
            }
            else if constexpr (std::is_same_v<T, dex::InvokeCustomInstruction>) {
                out.kind = DEXPP_INSN_INVOKE_CUSTOM;
                out.call_site_index = i.call_site_index;
                fill_regs(out, i.arg_regs);
            }
            else if constexpr (std::is_same_v<T, dex::UnaryOpInstruction>) {
                out.kind = DEXPP_INSN_UNARY_OP;
                out.dest = i.dest;
                out.src = i.src;
                out.dest_width = operand_width(i.dest_width);
                out.src_width = operand_width(i.src_width);
            }
            else if constexpr (std::is_same_v<T, dex::BinaryOpInstruction>) {
                out.kind = DEXPP_INSN_BINARY_OP;
                out.dest = i.dest;
                out.src_a = i.src_a;
                out.src_b = i.src_b;
                out.width = operand_width(i.width);
            }
            else if constexpr (std::is_same_v<T, dex::BinaryLitInstruction>) {
                out.kind = DEXPP_INSN_BINARY_LIT;
                out.dest = i.dest;
                out.src = i.src;
                out.literal = i.literal;
            }
            else {
                static_assert(always_false<T>,
                              "a new instruction type needs a DexppInstructionKind");
            }
        },
        insn);
    return out;
}

// ===== instructions: C -> C++ (for to_string) ================================

std::vector<uint16_t> regs_of(const DexppInstruction &in)
{
    if (in.arg_regs == nullptr)
        return {};
    return std::vector<uint16_t>(in.arg_regs, in.arg_regs + in.arg_count);
}

std::optional<dex::Instruction> instruction(const DexppInstruction &in)
{
    const dex::InstructionBase base{in.offset, in.opcode, in.size};
    const auto u8 = [](auto v) { return static_cast<uint8_t>(v); };
    // raw_enum: the caller may have stored a kind this header does not list.
    switch (raw_enum(in.kind)) {
    case DEXPP_INSN_NOP:
        return dex::NopInstruction{base};
    case DEXPP_INSN_GOTO:
        return dex::GotoInstruction{base, in.branch_offset};
    case DEXPP_INSN_IF:
        return dex::IfInstruction{base, in.branch_offset, in.reg_a, in.reg_b};
    case DEXPP_INSN_IF_ZERO:
        return dex::IfZeroInstruction{base, in.branch_offset, in.reg};
    case DEXPP_INSN_SWITCH: {
        std::vector<dex::SwitchCase> cases;
        for (std::size_t c = 0; in.cases != nullptr && c < in.case_count; ++c)
            cases.push_back(dex::SwitchCase{in.cases[c].key, in.cases[c].target});
        return dex::SwitchInstruction{base, in.payload_offset, in.reg, in.is_packed != 0,
                                      std::move(cases)};
    }
    case DEXPP_INSN_RETURN:
        return dex::ReturnInstruction{
            base, in.has_reg != 0 ? std::optional<uint8_t>(in.reg) : std::nullopt,
            operand_width(in.width)};
    case DEXPP_INSN_THROW:
        return dex::ThrowInstruction{base, in.reg};
    case DEXPP_INSN_MOVE:
        return dex::MoveInstruction{base, in.dest, in.src, operand_width(in.width)};
    case DEXPP_INSN_CONST:
        return dex::ConstInstruction{base, u8(in.dest), in.value, operand_width(in.width)};
    case DEXPP_INSN_CONST_STRING:
        return dex::ConstStringInstruction{base, u8(in.dest), in.string_index};
    case DEXPP_INSN_CONST_CLASS:
        return dex::ConstClassInstruction{base, u8(in.dest), in.type_index};
    case DEXPP_INSN_CONST_HANDLE:
        return dex::ConstHandleInstruction{base, u8(in.dest), in.index};
    case DEXPP_INSN_TYPE:
        return dex::TypeInstruction{base, in.type_index, u8(in.dest),
                                    in.has_src != 0 ? std::optional<uint8_t>(u8(in.src))
                                                    : std::nullopt};
    case DEXPP_INSN_FILLED_NEW_ARRAY:
        return dex::FilledNewArrayInstruction{base, in.type_index, regs_of(in)};
    case DEXPP_INSN_MONITOR:
        return dex::MonitorInstruction{base, in.reg, in.is_enter != 0};
    case DEXPP_INSN_FILL_ARRAY:
        return dex::FillArrayInstruction{base, in.reg, in.payload_offset};
    case DEXPP_INSN_COMPARE:
        return dex::CompareInstruction{base, u8(in.dest), in.src_a, in.src_b,
                                       operand_width(in.src_width)};
    case DEXPP_INSN_ARRAY_OP:
        return dex::ArrayOpInstruction{base,         in.value_reg,     in.array_reg,
                                       in.index_reg, in.is_write != 0, operand_width(in.width)};
    case DEXPP_INSN_FIELD:
        return dex::FieldInstruction{base,
                                     in.field_index,
                                     in.value_reg,
                                     in.has_object_reg != 0 ? std::optional<uint8_t>(in.object_reg)
                                                            : std::nullopt,
                                     in.is_static != 0,
                                     in.is_write != 0,
                                     operand_width(in.width)};
    case DEXPP_INSN_INVOKE:
        return dex::InvokeInstruction{base, in.method_index, regs_of(in)};
    case DEXPP_INSN_INVOKE_CUSTOM:
        return dex::InvokeCustomInstruction{base, in.call_site_index, regs_of(in)};
    case DEXPP_INSN_UNARY_OP:
        return dex::UnaryOpInstruction{base, u8(in.dest), u8(in.src), operand_width(in.dest_width),
                                       operand_width(in.src_width)};
    case DEXPP_INSN_BINARY_OP:
        return dex::BinaryOpInstruction{base, u8(in.dest), in.src_a, in.src_b,
                                        operand_width(in.width)};
    case DEXPP_INSN_BINARY_LIT:
        return dex::BinaryLitInstruction{base, u8(in.dest), u8(in.src),
                                         static_cast<int16_t>(in.literal)};
    }
    return std::nullopt; // a kind the C++ side does not know
}

void clear(DexppInstruction &insn)
{
    std::free(insn.cases);
    std::free(insn.arg_regs);
    insn.cases = nullptr;
    insn.arg_regs = nullptr;
}

// ===== annotations ===========================================================

void clear(DexppAnnotation &a);

DexppAnnotation annotation(const dex::Annotation &a)
{
    DexppAnnotation out{};
    out.type_descriptor = cstr(a.type_descriptor);
    out.visibility = visibility(a.visibility);
    out.element_count = a.elements.size();
    out.elements = alloc_array<DexppAnnotationElement>(a.elements.size());
    for (std::size_t i = 0; i < a.elements.size(); ++i) {
        out.elements[i].name = cstr(a.elements[i].name);
        out.elements[i].value = value(a.elements[i].value);
    }
    return out;
}

void clear(DexppAnnotation &a)
{
    std::free(a.type_descriptor);
    for (std::size_t i = 0; a.elements != nullptr && i < a.element_count; ++i) {
        std::free(a.elements[i].name);
        dexpp_c::clear(a.elements[i].value); // declared outside this namespace
    }
    std::free(a.elements);
    a = DexppAnnotation{};
}

} // namespace

DexppValue value(const dex::AnnotationValue &v)
{
    DexppValue out{};
    std::visit(
        [&](const auto &x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) {
                out.kind = DEXPP_VALUE_NULL;
            }
            else if constexpr (std::is_same_v<T, bool>) {
                out.kind = DEXPP_VALUE_BOOL;
                out.bool_value = x ? 1 : 0;
            }
            else if constexpr (std::is_same_v<T, int64_t>) {
                out.kind = DEXPP_VALUE_INT;
                out.int_value = x;
            }
            else if constexpr (std::is_same_v<T, double>) {
                out.kind = DEXPP_VALUE_FLOAT;
                out.float_value = x;
            }
            else if constexpr (std::is_same_v<T, std::string>) {
                out.kind = DEXPP_VALUE_STRING;
                out.string_value = text(x);
            }
            else if constexpr (std::is_same_v<T, dex::AnnotationValue::TypeRef>) {
                out.kind = DEXPP_VALUE_TYPE;
                out.type_descriptor = cstr(x.descriptor);
            }
            else if constexpr (std::is_same_v<T, dex::AnnotationValue::EnumRef>) {
                out.kind = DEXPP_VALUE_ENUM;
                out.field = field_id(x.constant);
            }
            else if constexpr (std::is_same_v<T, dex::AnnotationValue::FieldRef>) {
                out.kind = DEXPP_VALUE_FIELD;
                out.field = field_id(x.field);
            }
            else if constexpr (std::is_same_v<T, dex::AnnotationValue::MethodRef>) {
                out.kind = DEXPP_VALUE_METHOD;
                out.method = method_id(x.method);
            }
            else if constexpr (std::is_same_v<T, dex::AnnotationValue::MethodTypeRef>) {
                out.kind = DEXPP_VALUE_METHOD_TYPE;
                out.proto = cstr(x.proto);
            }
            else if constexpr (std::is_same_v<T, dex::AnnotationValue::MethodHandleRef>) {
                out.kind = DEXPP_VALUE_METHOD_HANDLE;
                out.method_handle_index = x.index;
            }
            else if constexpr (std::is_same_v<T, std::vector<dex::AnnotationValue>>) {
                out.kind = DEXPP_VALUE_ARRAY;
                out.element_count = x.size();
                out.elements = alloc_array<DexppValue>(x.size());
                for (std::size_t i = 0; i < x.size(); ++i)
                    out.elements[i] = value(x[i]);
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<const dex::Annotation>>) {
                out.kind = DEXPP_VALUE_ANNOTATION;
                out.annotation = alloc_array<DexppAnnotation>(1);
                if (x)
                    *out.annotation = annotation(*x);
            }
            else {
                static_assert(always_false<T>, "a new value type needs a DexppValueKind");
            }
        },
        v.value);
    return out;
}

void clear(DexppValue &v)
{
    clear(v.string_value);
    std::free(v.type_descriptor);
    std::free(v.proto);
    clear(v.field);
    clear(v.method);
    for (std::size_t i = 0; v.elements != nullptr && i < v.element_count; ++i)
        clear(v.elements[i]);
    std::free(v.elements);
    if (v.annotation != nullptr)
        clear(*v.annotation);
    std::free(v.annotation);
    v = DexppValue{};
}

DexppAnnotation *annotation_array(const std::vector<dex::Annotation> &items, size_t *count)
{
    *count = items.size();
    DexppAnnotation *out = alloc_array<DexppAnnotation>(items.size());
    for (std::size_t i = 0; i < items.size(); ++i)
        out[i] = annotation(items[i]);
    return out;
}

} // namespace dexpp_c

using namespace dexpp_c;

extern "C" {

/* ---- Values and annotations --------------------------------------------- */

void dexpp_value_clear(DexppValue *value) { clear(*value); }

void dexpp_annotation_array_free(DexppAnnotation *annotations, size_t count)
{
    if (annotations == nullptr)
        return;
    for (size_t i = 0; i < count; ++i)
        clear(annotations[i]);
    std::free(annotations);
}

DexppAnnotation *dexpp_method_annotations(const DexppMethod *m, size_t *count)
{
    return annotation_array(m->method.annotations(), count);
}

/* ---- Instructions ------------------------------------------------------- */

DexppInstruction *dexpp_method_instructions(const DexppMethod *m, size_t *count)
{
    std::span<const dex::Instruction> insns = m->method.instructions();
    *count = insns.size();
    DexppInstruction *out = alloc_array<DexppInstruction>(insns.size());
    for (size_t i = 0; i < insns.size(); ++i)
        out[i] = instruction(insns[i]);
    return out;
}

void dexpp_instruction_array_free(DexppInstruction *insns, size_t count)
{
    if (insns == nullptr)
        return;
    for (size_t i = 0; i < count; ++i)
        clear(insns[i]);
    std::free(insns);
}

const char *dexpp_opcode_name(uint8_t opcode)
{
    // Copies with a final NUL, built once (thread-safe static initialization).
    static const std::array<std::string, 256> names = [] {
        std::array<std::string, 256> out;
        for (std::size_t op = 0; op < out.size(); ++op)
            out[op] = std::string(dex::opcode_name(static_cast<uint8_t>(op)));
        return out;
    }();
    return names[opcode].c_str();
}

char *dexpp_instruction_to_string(const DexppInstruction *insn)
{
    std::optional<dex::Instruction> rebuilt = instruction(*insn);
    if (!rebuilt)
        return cstr(dexpp_opcode_name(insn->opcode));
    return cstr(dex::to_string(*rebuilt));
}

/* ---- Control-flow graph ------------------------------------------------- */

DexppBasicBlock *dexpp_method_cfg(const DexppMethod *m, size_t *count)
{
    std::span<const dex::BasicBlock> blocks = m->method.cfg().blocks();
    *count = blocks.size();
    DexppBasicBlock *out = alloc_array<DexppBasicBlock>(blocks.size());
    for (size_t i = 0; i < blocks.size(); ++i) {
        const dex::BasicBlock &b = blocks[i];
        DexppBasicBlock &o = out[i];
        o.index = b.index;
        o.start_offset = b.start_offset;
        o.end_offset = b.end_offset;
        o.first_insn = b.first_insn;
        o.insn_count = b.insn_count;
        o.is_handler_entry = b.is_handler_entry ? 1 : 0;
        o.successor_count = b.successors.size();
        o.successors = alloc_array<DexppCfgEdge>(b.successors.size());
        for (size_t e = 0; e < b.successors.size(); ++e) {
            const dex::CfgEdge &edge = b.successors[e];
            o.successors[e] = DexppCfgEdge{edge.target_block, cfg_edge_kind(edge.kind),
                                           edge.label.has_value() ? 1 : 0, edge.label.value_or(0)};
        }
        o.predecessors = index_array(b.predecessors, &o.predecessor_count);
    }
    return out;
}

void dexpp_basic_block_array_free(DexppBasicBlock *blocks, size_t count)
{
    if (blocks == nullptr)
        return;
    for (size_t i = 0; i < count; ++i) {
        std::free(blocks[i].successors);
        std::free(blocks[i].predecessors);
    }
    std::free(blocks);
}

/* ---- Registers, references, try blocks ---------------------------------- */

DexppParameterRegister *dexpp_method_parameter_registers(const DexppMethod *m, size_t *count)
{
    std::vector<dex::ParameterRegister> regs = m->method.parameter_registers();
    *count = regs.size();
    DexppParameterRegister *out = alloc_array<DexppParameterRegister>(regs.size());
    for (size_t i = 0; i < regs.size(); ++i)
        out[i] = DexppParameterRegister{regs[i].reg, cstr(regs[i].type.descriptor()),
                                        regs[i].is_this ? 1 : 0, regs[i].is_wide ? 1 : 0};
    return out;
}

void dexpp_parameter_register_array_free(DexppParameterRegister *regs, size_t count)
{
    if (regs == nullptr)
        return;
    for (size_t i = 0; i < count; ++i)
        std::free(regs[i].type);
    std::free(regs);
}

DexppMethodCall *dexpp_method_calls(const DexppMethod *m, size_t *count)
{
    std::vector<dex::MethodCall> calls = m->method.calls();
    *count = calls.size();
    DexppMethodCall *out = alloc_array<DexppMethodCall>(calls.size());
    for (size_t i = 0; i < calls.size(); ++i)
        out[i] = DexppMethodCall{calls[i].code_offset, method_id(calls[i].target),
                                 invoke_kind(calls[i].kind)};
    return out;
}

void dexpp_method_call_array_free(DexppMethodCall *calls, size_t count)
{
    if (calls == nullptr)
        return;
    for (size_t i = 0; i < count; ++i)
        clear(calls[i].target);
    std::free(calls);
}

DexppFieldAccess *dexpp_method_field_accesses(const DexppMethod *m, size_t *count)
{
    std::vector<dex::FieldAccess> accesses = m->method.field_accesses();
    *count = accesses.size();
    DexppFieldAccess *out = alloc_array<DexppFieldAccess>(accesses.size());
    for (size_t i = 0; i < accesses.size(); ++i)
        out[i] = DexppFieldAccess{accesses[i].code_offset, field_id(accesses[i].field),
                                  accesses[i].is_write ? 1 : 0, accesses[i].is_static ? 1 : 0};
    return out;
}

void dexpp_field_access_array_free(DexppFieldAccess *accesses, size_t count)
{
    if (accesses == nullptr)
        return;
    for (size_t i = 0; i < count; ++i)
        clear(accesses[i].field);
    std::free(accesses);
}

DexppStringLoad *dexpp_method_string_loads(const DexppMethod *m, size_t *count)
{
    std::vector<dex::StringLoad> loads = m->method.string_loads();
    *count = loads.size();
    DexppStringLoad *out = alloc_array<DexppStringLoad>(loads.size());
    for (size_t i = 0; i < loads.size(); ++i)
        out[i] = DexppStringLoad{loads[i].code_offset, text(loads[i].value)};
    return out;
}

void dexpp_string_load_array_free(DexppStringLoad *loads, size_t count)
{
    if (loads == nullptr)
        return;
    for (size_t i = 0; i < count; ++i)
        clear(loads[i].value);
    std::free(loads);
}

DexppTypeUse *dexpp_method_type_uses(const DexppMethod *m, size_t *count)
{
    std::vector<dex::TypeUse> uses = m->method.type_uses();
    *count = uses.size();
    DexppTypeUse *out = alloc_array<DexppTypeUse>(uses.size());
    for (size_t i = 0; i < uses.size(); ++i)
        out[i] = DexppTypeUse{uses[i].code_offset, cstr(uses[i].descriptor),
                              type_ref_kind(uses[i].kind)};
    return out;
}

void dexpp_type_use_array_free(DexppTypeUse *uses, size_t count)
{
    if (uses == nullptr)
        return;
    for (size_t i = 0; i < count; ++i)
        std::free(uses[i].descriptor);
    std::free(uses);
}

DexppTryBlock *dexpp_method_try_blocks(const DexppMethod *m, size_t *count)
{
    std::vector<dex::TryBlock> blocks = m->method.try_blocks();
    *count = blocks.size();
    DexppTryBlock *out = alloc_array<DexppTryBlock>(blocks.size());
    for (size_t i = 0; i < blocks.size(); ++i) {
        const dex::TryBlock &b = blocks[i];
        DexppTryBlock &o = out[i];
        o.start_offset = b.start_offset;
        o.end_offset = b.end_offset;
        o.has_catch_all = b.catch_all_offset.has_value() ? 1 : 0;
        o.catch_all_offset = b.catch_all_offset.value_or(0);
        o.handler_count = b.handlers.size();
        o.handlers = alloc_array<DexppCatchHandler>(b.handlers.size());
        for (size_t h = 0; h < b.handlers.size(); ++h)
            o.handlers[h] = DexppCatchHandler{cstr(b.handlers[h].type_descriptor),
                                              b.handlers[h].handler_offset};
    }
    return out;
}

void dexpp_try_block_array_free(DexppTryBlock *blocks, size_t count)
{
    if (blocks == nullptr)
        return;
    for (size_t i = 0; i < count; ++i) {
        for (size_t h = 0; h < blocks[i].handler_count; ++h)
            std::free(blocks[i].handlers[h].type_descriptor);
        std::free(blocks[i].handlers);
    }
    std::free(blocks);
}

} // extern "C"
