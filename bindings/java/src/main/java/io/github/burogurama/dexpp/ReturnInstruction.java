package io.github.burogurama.dexpp;

import java.util.Objects;
import java.util.OptionalInt;

/**
 * {@code return-void}, {@code return}, {@code return-wide} and {@code return-object}.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 * @param reg the register that holds the returned value; empty for {@code return-void}
 * @param width the width of the returned value
 */
public record ReturnInstruction(
        int offset, int opcode, int size, OptionalInt reg, OperandWidth width)
        implements Instruction {
    /** Checks the parts. */
    public ReturnInstruction {
        Objects.requireNonNull(reg, "reg");
        Objects.requireNonNull(width, "width");
    }
}
