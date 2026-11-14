#pragma once

#include <string>

namespace dex {

/** Analysis errors*/
struct AnalysisError
{
    enum class Code {
        FileNotFound,           ///< Path does not exist.
        CannotRead,             ///< Path exists but cannot be read (a directory, no permission).
        InvalidDexFile,         ///< DEX file  parsing failed (malformed DEX bytes).
        InvalidApk,             ///< APK could not be opened or contains no classes.dex.
        MalformedBytecode,      ///< Bytecode within a code item is structurally invalid.
        UnresolvedReference,    ///< A referenced class/method/field is not in any loaded DEX.
        UnsupportedInstruction, ///< An instruction opcode is not yet implemented.
        InvalidDescriptor       ///< A type descriptor string is not well-formed.
    };

    Code code;
    std::string message;
};

} // namespace dex
