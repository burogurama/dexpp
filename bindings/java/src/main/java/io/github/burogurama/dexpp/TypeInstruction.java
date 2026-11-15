package io.github.burogurama.dexpp;

import java.util.Objects;
import java.util.OptionalInt;

/**
 * {@code check-cast}, {@code instance-of}, {@code new-instance} and {@code new-array}.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 * @param typeIndex the index in the DEX file's type table
 * @param dest the register the instruction works on: the register that is cast for {@code
 *     check-cast}, the result for {@code instance-of} and {@code new-array}, and the new object for
 *     {@code new-instance}
 * @param src the object register for {@code instance-of}, and the size register for {@code
 *     new-array}; empty for {@code check-cast} and {@code new-instance}, which use one register
 */
public record TypeInstruction(
        int offset, int opcode, int size, int typeIndex, int dest, OptionalInt src)
        implements Instruction {
    /** Checks the parts. */
    public TypeInstruction {
        Objects.requireNonNull(src, "src");
    }
}
