#include "disassembly.hpp"

#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "analysis_context_impl.hpp"
#include "method.hpp"
#include "raw/opcodes.hpp"
#include "raw/parser.hpp"

namespace dex {

namespace detail {

namespace {

template <typename T> inline constexpr bool always_false = false;

// Code points that hide text or change how a line reads: C1 controls, the
// soft hyphen, zero-width and joiner characters, line and paragraph
// separators, bidirectional marks, embeddings, overrides and isolates,
// invisible operators, the byte order mark, interlinear annotation
// characters, and surrogates (only present as lone ones).
bool hides_text(uint32_t cp)
{
    return (cp >= 0x80 && cp <= 0x9f) || cp == 0xad || cp == 0x61c || cp == 0x180e ||
           (cp >= 0x200b && cp <= 0x200f) || (cp >= 0x2028 && cp <= 0x202e) ||
           (cp >= 0x2060 && cp <= 0x2064) || (cp >= 0x2066 && cp <= 0x206f) ||
           (cp >= 0xd800 && cp <= 0xdfff) || cp == 0xfeff || (cp >= 0xfff9 && cp <= 0xfffb);
}

// The length of the UTF-8 sequence at s[i] and its code point, or 0 when the
// bytes there are not a well-formed sequence (lone surrogates, as WTF-8
// writes them, count as well-formed).
std::size_t utf8_at(std::string_view s, std::size_t i, uint32_t &cp)
{
    const auto c = static_cast<unsigned char>(s[i]);
    std::size_t len = c >= 0xf8 ? 0 : c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc2 ? 2 : 0;
    if (len == 0 || s.size() - i < len)
        return 0;
    cp = c & (0x7f >> len);
    for (std::size_t k = 1; k < len; ++k) {
        const auto cc = static_cast<unsigned char>(s[i + k]);
        if ((cc & 0xc0) != 0x80)
            return 0;
        cp = (cp << 6) | (cc & 0x3f);
    }
    // Overlong forms and values past U+10FFFF are not UTF-8.
    if ((len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10ffff)))
        return 0;
    return len;
}

// A signed literal in smali's form: 0x1f, -0x1.
std::string hex(int64_t value)
{
    if (value < 0)
        return std::format("-0x{:x}", 0 - static_cast<uint64_t>(value));
    return std::format("0x{:x}", value);
}

// A code offset as dexdump prints it: four or more hex digits.  A target
// outside the method's code gets a question mark.
std::string target(int64_t at, std::size_t code_units)
{
    if (at < 0)
        return std::format("-{:04x}?", 0 - static_cast<uint64_t>(at));
    if (static_cast<uint64_t>(at) >= code_units)
        return std::format("{:04x}?", at);
    return std::format("{:04x}", at);
}

std::string reg(uint32_t r) { return std::format("v{}", r); }

// {v0, v1} for the 35c formats, {v0 .. v3} for the /range (3rc) forms.
std::string reg_list(std::span<const uint16_t> regs, bool range)
{
    if (regs.empty())
        return "{}";
    if (range)
        return std::format("{{v{} .. v{}}}", regs.front(), regs.back());
    std::string out = "{";
    for (std::size_t k = 0; k < regs.size(); ++k) {
        if (k)
            out += ", ";
        out += reg(regs[k]);
    }
    return out + "}";
}

bool is_range(uint8_t op)
{
    return op == 0x25 || (op >= 0x74 && op <= 0x78) || op == 0xfb || op == 0xfd;
}

// A name from the pool, or "kind@N?" when the index is out of range.
std::string ref(const std::optional<std::string> &name, std::string_view kind, uint32_t index)
{
    return name ? *name : std::format("{}@{}?", kind, index);
}

// Where a payload starts, when a valid payload with ident @p ident starts
// there and fits in the code.
std::optional<uint32_t> payload_at(std::span<const uint16_t> insns, uint32_t insn_offset,
                                   int32_t payload_offset, uint16_t ident)
{
    const int64_t at = static_cast<int64_t>(insn_offset) + payload_offset;
    if (at < 0 || static_cast<uint64_t>(at) >= insns.size())
        return std::nullopt;
    const auto p = static_cast<uint32_t>(at);
    if (insns[p] != ident)
        return std::nullopt;
    std::optional<uint64_t> units = raw::payload_units(insns, p);
    if (!units || *units > insns.size() - p)
        return std::nullopt;
    return p;
}

std::string switch_operand(const SwitchInstruction &sw, std::span<const uint16_t> insns)
{
    const uint16_t ident = sw.is_packed ? 0x0100 : 0x0200;
    const uint32_t at = sw.base.offset;
    if (!payload_at(insns, at, sw.payload_offset, ident)) {
        const int64_t where = static_cast<int64_t>(at) + sw.payload_offset;
        std::string text = target(where, insns.size());
        return text.ends_with('?') ? text : text + "?";
    }
    std::string out = "{";
    for (std::size_t k = 0; k < sw.cases.size(); ++k) {
        if (k)
            out += ", ";
        out += hex(sw.cases[k].key) + " -> " +
               target(static_cast<int64_t>(at) + sw.cases[k].target, insns.size());
    }
    return out + "}";
}

std::string array_data_operand(const FillArrayInstruction &fill, std::span<const uint16_t> insns)
{
    const uint32_t at = fill.base.offset;
    std::optional<uint32_t> p = payload_at(insns, at, fill.payload_offset, 0x0300);
    const uint16_t width = p ? insns[*p + 1] : 0;
    if (!p || (width != 1 && width != 2 && width != 4 && width != 8)) {
        const int64_t where = static_cast<int64_t>(at) + fill.payload_offset;
        std::string text = target(where, insns.size());
        return text.ends_with('?') ? text : text + "?";
    }
    const uint32_t count = insns[*p + 2] | (static_cast<uint32_t>(insns[*p + 3]) << 16);
    // The data is a little-endian byte array starting at code unit p + 4.
    auto byte = [&](uint64_t k) {
        return static_cast<uint8_t>(insns[*p + 4 + k / 2] >> (8 * (k % 2)));
    };
    const char *suffix = width == 1 ? "t" : width == 2 ? "s" : width == 8 ? "L" : "";
    std::string out = "[";
    for (uint32_t j = 0; j < count; ++j) {
        uint64_t bits = 0;
        for (uint32_t b = 0; b < width; ++b)
            bits |= static_cast<uint64_t>(byte(uint64_t{j} * width + b)) << (8 * b);
        // Sign-extend from the element width.
        const unsigned shift = 64 - 8 * width;
        const auto value = static_cast<int64_t>(bits << shift) >> shift;
        if (j)
            out += ", ";
        out += hex(value) + suffix;
    }
    return out + "]";
}

} // namespace

std::string escape_text(std::string_view text, bool quoted)
{
    std::string out;
    out.reserve(text.size() + (quoted ? 2 : 0));
    if (quoted)
        out += '"';
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '"':
                out += quoted ? "\\\"" : "\"";
                break;
            default:
                if (c < 0x20 || c == 0x7f)
                    out += std::format("\\u{:04x}", c);
                else
                    out += static_cast<char>(c);
            }
            ++i;
            continue;
        }
        uint32_t cp = 0;
        const std::size_t len = utf8_at(text, i, cp);
        if (len == 0) {
            out += "\\ufffd";
            ++i;
        }
        else {
            if (hides_text(cp))
                out += std::format("\\u{:04x}", cp);
            else
                out.append(text.substr(i, len));
            i += len;
        }
    }
    if (quoted)
        out += '"';
    return out;
}

std::string_view method_handle_kind_name(uint8_t kind)
{
    static constexpr std::string_view kNames[] = {
        "static-put",         "static-get",    "instance-put",
        "instance-get",       "invoke-static", "invoke-instance",
        "invoke-constructor", "invoke-direct", "invoke-interface",
    };
    return kind < std::size(kNames) ? kNames[kind] : std::string_view{};
}

std::string disassemble_instruction(const Instruction &insn, std::span<const uint16_t> insns,
                                    const PoolNames &names)
{
    const InstructionBase &base = base_of(insn);
    const auto here = static_cast<int64_t>(base.offset);
    std::string operands = std::visit(
        [&](const auto &i) -> std::string {
            using T = std::decay_t<decltype(i)>;
            if constexpr (std::is_same_v<T, NopInstruction> ||
                          std::is_same_v<T, InvalidInstruction>) {
                return "";
            }
            else if constexpr (std::is_same_v<T, GotoInstruction>) {
                return target(here + i.branch_offset, insns.size());
            }
            else if constexpr (std::is_same_v<T, IfInstruction>) {
                return std::format("v{}, v{}, {}", i.reg_a, i.reg_b,
                                   target(here + i.branch_offset, insns.size()));
            }
            else if constexpr (std::is_same_v<T, IfZeroInstruction>) {
                return std::format("v{}, {}", i.reg, target(here + i.branch_offset, insns.size()));
            }
            else if constexpr (std::is_same_v<T, SwitchInstruction>) {
                return std::format("v{}, {}", i.reg, switch_operand(i, insns));
            }
            else if constexpr (std::is_same_v<T, ReturnInstruction>) {
                return i.reg ? reg(*i.reg) : std::string{};
            }
            else if constexpr (std::is_same_v<T, ThrowInstruction> ||
                               std::is_same_v<T, MonitorInstruction>) {
                return reg(i.reg);
            }
            else if constexpr (std::is_same_v<T, MoveInstruction>) {
                // move-result* / move-exception name no source register.
                return i.src ? std::format("v{}, v{}", i.dest, *i.src) : reg(i.dest);
            }
            else if constexpr (std::is_same_v<T, ConstInstruction>) {
                return std::format("v{}, {}{}", i.dest, hex(i.value),
                                   i.width == OperandWidth::Pair ? "L" : "");
            }
            else if constexpr (std::is_same_v<T, ConstStringInstruction>) {
                std::optional<std::string> s = names.string(i.string_index);
                return std::format("v{}, {}", i.dest,
                                   s ? escape_text(*s, true)
                                     : std::format("string@{}?", i.string_index));
            }
            else if constexpr (std::is_same_v<T, ConstClassInstruction>) {
                return std::format("v{}, {}", i.dest,
                                   ref(names.type(i.type_index), "type", i.type_index));
            }
            else if constexpr (std::is_same_v<T, ConstMethodHandleInstruction>) {
                return std::format("v{}, {}", i.dest,
                                   ref(names.method_handle(i.method_handle_index), "method_handle",
                                       i.method_handle_index));
            }
            else if constexpr (std::is_same_v<T, ConstMethodTypeInstruction>) {
                return std::format("v{}, {}", i.dest,
                                   ref(names.proto(i.proto_index), "proto", i.proto_index));
            }
            else if constexpr (std::is_same_v<T, TypeInstruction>) {
                std::string type = ref(names.type(i.type_index), "type", i.type_index);
                if (i.src)
                    return std::format("v{}, v{}, {}", i.dest, *i.src, type);
                return std::format("v{}, {}", i.dest, type);
            }
            else if constexpr (std::is_same_v<T, FilledNewArrayInstruction>) {
                return reg_list(i.arg_regs, is_range(base.opcode)) + ", " +
                       ref(names.type(i.type_index), "type", i.type_index);
            }
            else if constexpr (std::is_same_v<T, FillArrayInstruction>) {
                return std::format("v{}, {}", i.reg, array_data_operand(i, insns));
            }
            else if constexpr (std::is_same_v<T, CompareInstruction>) {
                return std::format("v{}, v{}, v{}", i.dest, i.src_a, i.src_b);
            }
            else if constexpr (std::is_same_v<T, ArrayOpInstruction>) {
                return std::format("v{}, v{}, v{}", i.value_reg, i.array_reg, i.index_reg);
            }
            else if constexpr (std::is_same_v<T, ArrayLengthInstruction>) {
                return std::format("v{}, v{}", i.dest, i.array_reg);
            }
            else if constexpr (std::is_same_v<T, FieldInstruction>) {
                std::string field = ref(names.field(i.field_index), "field", i.field_index);
                if (i.object_reg)
                    return std::format("v{}, v{}, {}", i.value_reg, *i.object_reg, field);
                return std::format("v{}, {}", i.value_reg, field);
            }
            else if constexpr (std::is_same_v<T, InvokeInstruction>) {
                return reg_list(i.arg_regs, is_range(base.opcode)) + ", " +
                       ref(names.method(i.method_index), "method", i.method_index);
            }
            else if constexpr (std::is_same_v<T, InvokePolymorphicInstruction>) {
                return reg_list(i.arg_regs, is_range(base.opcode)) + ", " +
                       ref(names.method(i.method_index), "method", i.method_index) + ", " +
                       ref(names.proto(i.proto_index), "proto", i.proto_index);
            }
            else if constexpr (std::is_same_v<T, InvokeCustomInstruction>) {
                std::optional<std::string> site = names.call_site(i.call_site_index);
                return reg_list(i.arg_regs, is_range(base.opcode)) + ", " +
                       std::format("call_site@{}{}", i.call_site_index, site ? *site : "?");
            }
            else if constexpr (std::is_same_v<T, UnaryOpInstruction>) {
                return std::format("v{}, v{}", i.dest, i.src);
            }
            else if constexpr (std::is_same_v<T, BinaryOpInstruction>) {
                // The /2addr forms (0xb0-0xcf) name two registers: the first is
                // both an operand and the result.
                if (base.opcode >= 0xb0 && base.opcode <= 0xcf)
                    return std::format("v{}, v{}", i.dest, i.src_b);
                return std::format("v{}, v{}, v{}", i.dest, i.src_a, i.src_b);
            }
            else if constexpr (std::is_same_v<T, BinaryLitInstruction>) {
                return std::format("v{}, v{}, {}", i.dest, i.src, hex(i.literal));
            }
            else {
                static_assert(always_false<T>, "a new instruction type needs its operands");
            }
        },
        insn);
    std::string_view name = opcode_name(base.opcode);
    if (operands.empty())
        return std::string(name);
    return std::format("{} {}", name, operands);
}

namespace {

// PoolNames over one DEX file of an analysis context.
class ContextNames final : public PoolNames
{
  public:
    ContextNames(const AnalysisContextImpl &impl, std::size_t dex_idx)
        : impl_(impl), dex_idx_(dex_idx), dex_(impl.dex_files()[dex_idx])
    {
    }

    std::optional<std::string> string(uint32_t index) const override
    {
        if (index >= dex_.string_ids().size())
            return std::nullopt;
        return std::string(impl_.string_of(dex_idx_, index));
    }

    std::optional<std::string> type(uint32_t index) const override
    {
        if (index >= dex_.type_ids().size())
            return std::nullopt;
        return escape_text(impl_.type_descriptor_of(dex_idx_, index), false);
    }

    std::optional<std::string> field(uint32_t index) const override
    {
        if (!impl_.field_item(dex_idx_, index))
            return std::nullopt;
        return field_text(impl_.field_id_of(dex_idx_, index));
    }

    std::optional<std::string> method(uint32_t index) const override
    {
        if (!impl_.method_item(dex_idx_, index))
            return std::nullopt;
        return method_text(impl_.method_id_of(dex_idx_, index));
    }

    std::optional<std::string> proto(uint32_t index) const override
    {
        if (!impl_.proto_item(dex_idx_, index))
            return std::nullopt;
        return escape_text(impl_.proto_descriptor_of(dex_idx_, index), false);
    }

    std::optional<std::string> method_handle(uint32_t index) const override
    {
        const auto handles = dex_.method_handles();
        constexpr auto kLastKind = static_cast<uint16_t>(MethodHandleKind::InvokeInterface);
        if (index >= handles.size() || handles[index].method_handle_type > kLastKind)
            return std::nullopt;
        const AnnotationValue::MethodHandleValue h = impl_.method_handle_of(dex_idx_, index);
        std::string member = std::holds_alternative<FieldId>(h.member)
                                 ? field_text(std::get<FieldId>(h.member))
                                 : method_text(std::get<MethodId>(h.member));
        return std::format("{}@{}", method_handle_kind_name(static_cast<uint8_t>(h.kind)), member);
    }

    std::optional<std::string> call_site(uint32_t index) const override
    {
        const auto ids = dex_.call_site_ids();
        if (index >= ids.size())
            return std::nullopt;
        uint32_t offset = ids[index].call_site_off;
        auto values = raw::parser::parse_encoded_array(impl_.buffer_of(dex_idx_), offset);
        // A call site is a bootstrap method handle, a method name and a
        // method type, then any extra arguments for the bootstrap method.
        using Type = raw::EncodedValue::Type;
        if (!values.has_value() || values->size() < 3 || (*values)[0].type != Type::MethodHandle ||
            (*values)[1].type != Type::String || (*values)[2].type != Type::MethodType)
            return std::nullopt;
        std::string out = "(";
        for (std::size_t k = 1; k < values->size(); ++k) {
            if (k > 1)
                out += ", ";
            out += value_text((*values)[k]);
        }
        out += ")@";
        // The bootstrap method is an invoke-static handle in any valid file;
        // print just its method, as smali does, and the whole handle otherwise.
        const uint32_t bootstrap = index_of((*values)[0]);
        std::optional<std::string> handle = method_handle(bootstrap);
        if (handle && handle->starts_with("invoke-static@"))
            out += handle->substr(std::string_view("invoke-static@").size());
        else
            out += ref(handle, "method_handle", bootstrap);
        return out;
    }

  private:
    static std::string field_text(const FieldId &id)
    {
        return escape_text(id.class_descriptor + "->" + id.name + ":" + id.type, false);
    }
    static std::string method_text(const MethodId &id)
    {
        return escape_text(id.class_descriptor + "->" + id.name + id.proto, false);
    }

    // The pool index an encoded value holds (UINT32_MAX, which no table
    // reaches, if it holds none).
    static uint32_t index_of(const raw::EncodedValue &v)
    {
        const auto *index = std::get_if<uint32_t>(&v.value);
        return index ? *index : UINT32_MAX;
    }

    // One encoded value (an extra argument of a call site) in smali's form.
    std::string value_text(const raw::EncodedValue &v) const
    {
        using Type = raw::EncodedValue::Type;
        auto index = [&] { return index_of(v); };
        auto integer = [&] {
            const auto *n = std::get_if<int64_t>(&v.value);
            return n ? *n : 0;
        };
        auto real = [&] {
            const auto *d = std::get_if<double>(&v.value);
            return d ? *d : 0.0;
        };
        switch (v.type) {
        case Type::Byte:
            return hex(integer()) + "t";
        case Type::Short:
            return hex(integer()) + "s";
        case Type::Char: {
            std::string c = escape_text(char_text(integer()), false);
            return "'" + (c == "'" ? "\\'" : c) + "'";
        }
        case Type::Int:
            return hex(integer());
        case Type::Long:
            return hex(integer()) + "L";
        case Type::Float:
            return number_text(real(), true);
        case Type::Double:
            return number_text(real(), false);
        case Type::MethodType:
            return ref(proto(index()), "proto", index());
        case Type::MethodHandle:
            return ref(method_handle(index()), "method_handle", index());
        case Type::String: {
            std::optional<std::string> s = string(index());
            return s ? escape_text(*s, true) : std::format("string@{}?", index());
        }
        case Type::Type:
            return ref(type(index()), "type", index());
        case Type::Field:
        case Type::Enum:
            return ref(field(index()), "field", index());
        case Type::Method:
            return ref(method(index()), "method", index());
        case Type::Array: {
            std::string out = "{";
            if (const auto *items = std::get_if<std::vector<raw::EncodedValue>>(&v.value))
                for (std::size_t k = 0; k < items->size(); ++k) {
                    if (k)
                        out += ", ";
                    out += value_text((*items)[k]);
                }
            return out + "}";
        }
        case Type::Annotation: {
            const auto *a = std::get_if<std::unique_ptr<raw::EncodedAnnotation>>(&v.value);
            const uint32_t type_idx = a && *a ? (*a)->type_idx : UINT32_MAX;
            return "@" + ref(type(type_idx), "type", type_idx);
        }
        case Type::Null:
            return "null";
        case Type::Boolean: {
            const auto *b = std::get_if<bool>(&v.value);
            return b && *b ? "true" : "false";
        }
        }
        return "?";
    }

    // A UTF-16 code unit as UTF-8 (a surrogate becomes its WTF-8 form, which
    // escape_text prints as \uXXXX).
    static std::string char_text(int64_t unit)
    {
        const auto c = static_cast<uint32_t>(unit & 0xffff);
        std::string out;
        if (c < 0x80) {
            out += static_cast<char>(c);
        }
        else if (c < 0x800) {
            out += static_cast<char>(0xc0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3f));
        }
        else {
            out += static_cast<char>(0xe0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (c & 0x3f));
        }
        return out;
    }

    static std::string number_text(double value, bool is_float)
    {
        const char *suffix = is_float ? "f" : "";
        if (std::isnan(value))
            return std::format("NaN{}", suffix);
        if (std::isinf(value))
            return std::format("{}Infinity{}", value < 0 ? "-" : "", suffix);
        if (is_float)
            return std::format("{}f", static_cast<float>(value));
        return std::format("{}", value);
    }

    const AnalysisContextImpl &impl_;
    std::size_t dex_idx_;
    const raw::DexFile &dex_;
};

} // namespace

} // namespace detail

std::vector<DisassemblyLine> Method::disassemble() const
{
    const raw::CodeItem *code = impl_->get_code_item(dex_idx_, code_off_);
    if (!code)
        return {};
    const detail::ContextNames names(*impl_, dex_idx_);
    std::vector<DisassemblyLine> out;
    std::span<const Instruction> insns = instructions();
    out.reserve(insns.size());
    for (const Instruction &insn : insns)
        out.push_back(DisassemblyLine{base_of(insn).offset,
                                      detail::disassemble_instruction(insn, code->insns, names)});
    return out;
}

} // namespace dex
