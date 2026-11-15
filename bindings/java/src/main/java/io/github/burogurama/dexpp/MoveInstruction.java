package io.github.burogurama.dexpp;

import java.util.Objects;

/**
 * {@code move}, {@code move-wide}, {@code move-object} and their {@code /from16} and {@code /16}
 * forms, the {@code move-result} instructions, and {@code move-exception}.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 * @param dest the register written
 * @param src the register read; 0 for the {@code move-result} instructions and {@code
 *     move-exception}, which take their value from the last call or the caught exception
 * @param width the width of the value moved
 */
public record MoveInstruction(
        int offset, int opcode, int size, int dest, int src, OperandWidth width)
        implements Instruction {
    /** Checks the parts. */
    public MoveInstruction {
        Objects.requireNonNull(width, "width");
    }
}
