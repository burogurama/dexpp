#pragma once

// Private header: not part of the public API (src/dex.hpp does not include
// it).  The public way to disassemble is Method::disassemble().

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "analysis/instruction.hpp"

namespace dex::detail {

/** The names behind the pool indices of one DEX file, as disassembly text.
 *  Each function returns nullopt for an index the file does not have, and the
 *  disassembler then prints the raw index with a question mark. */
class PoolNames
{
  public:
    virtual ~PoolNames() = default;

    /** The string itself, not escaped or quoted. */
    virtual std::optional<std::string> string(uint32_t index) const = 0;
    /** A type descriptor, such as "Ljava/lang/String;". */
    virtual std::optional<std::string> type(uint32_t index) const = 0;
    /** A field, as "Lclass;->name:Type". */
    virtual std::optional<std::string> field(uint32_t index) const = 0;
    /** A method, as "Lclass;->name(Params)Return". */
    virtual std::optional<std::string> method(uint32_t index) const = 0;
    /** A prototype, as "(Params)Return". */
    virtual std::optional<std::string> proto(uint32_t index) const = 0;
    /** A method handle, as "kind@member", e.g. "invoke-static@LA;->f()V". */
    virtual std::optional<std::string> method_handle(uint32_t index) const = 0;
    /** What a call site holds, printed after "call_site@N": the method name,
     *  the method type and the extra arguments in parentheses, then "@" and
     *  the bootstrap method, e.g. `("run", ()Ljava/lang/Runnable;)@LA;->b(...)...`. */
    virtual std::optional<std::string> call_site(uint32_t index) const = 0;
};

/** @p text with what would hide or break a one-line rendering escaped:
 *  control characters, invisible format characters (such as U+200B and the
 *  bidirectional overrides) and lone surrogates as `\uXXXX`, plus `\n`, `\t`,
 *  `\r`, `\b`, `\f` and the backslash.  With @p quoted the result is in double
 *  quotes and `"` is escaped too.  Bytes that are not UTF-8 become `�`. */
std::string escape_text(std::string_view text, bool quoted);

/** The smali-style name of a method handle kind (0 to 8): "static-put",
 *  "invoke-static" and so on. */
std::string_view method_handle_kind_name(uint8_t kind);

/** One instruction as disassembly text: the opcode name, then the operands,
 *  with pool references resolved through @p names.  @p insns is the method's
 *  code, which payloads (switch tables, array data) are read from. */
std::string disassemble_instruction(const Instruction &insn, std::span<const uint16_t> insns,
                                    const PoolNames &names);

} // namespace dex::detail
