package io.github.burogurama.dexpp;

import java.util.Objects;

/**
 * The {@code aget} and {@code aput} family: read or write one element of an array.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 * @param valueReg the register the element is read into, or written from
 * @param arrayReg the register that holds the array
 * @param indexReg the register that holds the index
 * @param isWrite true for the {@code aput} instructions
 * @param width the width of the element
 */
public record ArrayOpInstruction(
        int offset,
        int opcode,
        int size,
        int valueReg,
        int arrayReg,
        int indexReg,
        boolean isWrite,
        OperandWidth width)
        implements Instruction {
    /** Checks the parts. */
    public ArrayOpInstruction {
        Objects.requireNonNull(width, "width");
    }
}
