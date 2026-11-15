package io.github.burogurama.dexpp;

import java.util.Objects;

/**
 * The {@code const} and {@code const-wide} forms: load a number into a register.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 * @param dest the register written
 * @param value the number, as a 64-bit integer for every form. The bytecode does not say whether a
 *     constant is an integer or a floating-point number, so a {@code float} or {@code double}
 *     constant appears here as its bit pattern
 * @param width {@link OperandWidth#PAIR} for the {@code const-wide} forms
 */
public record ConstInstruction(
        int offset, int opcode, int size, int dest, long value, OperandWidth width)
        implements Instruction {
    /** Checks the parts. */
    public ConstInstruction {
        Objects.requireNonNull(width, "width");
    }
}
