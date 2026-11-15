package io.github.burogurama.dexpp;

import java.util.Objects;

/**
 * One-operand arithmetic and conversions: {@code neg-int}, {@code not-long}, {@code int-to-long},
 * {@code double-to-float} and the like.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 * @param dest the register written
 * @param src the register read
 * @param destWidth the width of the result
 * @param srcWidth the width of the operand
 */
public record UnaryOpInstruction(
        int offset,
        int opcode,
        int size,
        int dest,
        int src,
        OperandWidth destWidth,
        OperandWidth srcWidth)
        implements Instruction {
    /** Checks the parts. */
    public UnaryOpInstruction {
        Objects.requireNonNull(destWidth, "destWidth");
        Objects.requireNonNull(srcWidth, "srcWidth");
    }
}
