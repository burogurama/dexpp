package io.github.burogurama.dexpp;

import java.util.Objects;

/**
 * {@code cmpl-float}, {@code cmpg-float}, {@code cmpl-double}, {@code cmpg-double} and {@code
 * cmp-long}: compare two values and store -1, 0 or 1.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 * @param dest the register that gets the result, always an {@code int}
 * @param srcA the first register compared
 * @param srcB the second register compared
 * @param srcWidth {@link OperandWidth#PAIR} when the operands are {@code long} or {@code double}
 */
public record CompareInstruction(
        int offset, int opcode, int size, int dest, int srcA, int srcB, OperandWidth srcWidth)
        implements Instruction {
    /** Checks the parts. */
    public CompareInstruction {
        Objects.requireNonNull(srcWidth, "srcWidth");
    }
}
