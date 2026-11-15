package io.github.burogurama.dexpp;

import java.util.Objects;
import java.util.OptionalInt;

/**
 * The {@code iget}, {@code iput}, {@code sget} and {@code sput} family: read or write a field.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 * @param fieldIndex the index in the DEX file's field table
 * @param valueReg the register the field is read into, or written from
 * @param objectReg the register that holds the object; empty for a static field
 * @param isStatic true for {@code sget} and {@code sput}
 * @param isWrite true for {@code iput} and {@code sput}
 * @param width the width of the field's value
 */
public record FieldInstruction(
        int offset,
        int opcode,
        int size,
        int fieldIndex,
        int valueReg,
        OptionalInt objectReg,
        boolean isStatic,
        boolean isWrite,
        OperandWidth width)
        implements Instruction {
    /** Checks the parts. */
    public FieldInstruction {
        Objects.requireNonNull(objectReg, "objectReg");
        Objects.requireNonNull(width, "width");
    }
}
