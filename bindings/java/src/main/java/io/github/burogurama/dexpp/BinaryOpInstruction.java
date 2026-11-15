package io.github.burogurama.dexpp;

import java.util.Objects;

/**
 * Two-operand arithmetic on registers: {@code add-int}, {@code sub-long}, {@code mul-double},
 * {@code shl-int} and the like, and their {@code /2addr} forms.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 * @param dest the register written
 * @param srcA the first operand; for a {@code /2addr} form, the same register as {@code dest}
 * @param srcB the second operand
 * @param width the width of the operation
 */
public record BinaryOpInstruction(
        int offset, int opcode, int size, int dest, int srcA, int srcB, OperandWidth width)
        implements Instruction {
    /** Checks the parts. */
    public BinaryOpInstruction {
        Objects.requireNonNull(width, "width");
    }
}
