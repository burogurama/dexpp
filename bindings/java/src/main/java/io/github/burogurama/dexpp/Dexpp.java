package io.github.burogurama.dexpp;

import java.lang.foreign.Arena;

/** The version of dex++, and the functions that need no handle. */
public final class Dexpp {
    private Dexpp() {}

    /**
     * The version of dex++ this package is built from, such as {@code "0.2.0"}. It is the version
     * in {@code src/version.hpp}; a test checks that they match.
     */
    public static final String VERSION = "0.2.0";

    /**
     * The name of a Dalvik opcode byte, such as {@code "invoke-virtual"} for {@code 0x6e}.
     *
     * @param opcode the opcode byte; only its low 8 bits are used
     * @return the name; an opcode the format does not use gives {@code "unused-XX"}, with the byte
     *     in hex
     */
    public static String opcodeName(int opcode) {
        String name = Mem.cString(Native.dexpp_opcode_name((byte) opcode));
        return name == null ? "" : name;
    }

    /**
     * Writes one instruction as text: the opcode name, then its operands, such as {@code
     * "const-string v0, string@12"}. Operands that point into a table of the DEX file are shown as
     * raw indices ({@code string@12}, {@code method@4}); an instruction alone cannot turn them into
     * names.
     *
     * <p>It works for decoded instructions and for records made by hand.
     *
     * @param insn the instruction
     * @return the text
     */
    public static String toString(Instruction insn) {
        try (Arena arena = Arena.ofConfined()) {
            return Mem.takeString(Native.dexpp_instruction_to_string(Code.toC(arena, insn)));
        }
    }
}
