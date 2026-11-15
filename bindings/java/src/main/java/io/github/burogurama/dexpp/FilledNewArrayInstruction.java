package io.github.burogurama.dexpp;

import java.util.List;

/**
 * {@code filled-new-array} and {@code filled-new-array/range}: create an array and fill it from
 * registers.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 * @param typeIndex the index of the array type in the DEX file's type table
 * @param argRegs the registers that hold the elements, in order
 */
public record FilledNewArrayInstruction(
        int offset, int opcode, int size, int typeIndex, List<Integer> argRegs)
        implements Instruction {
    /** Checks the parts, and copies the lists into immutable lists. */
    public FilledNewArrayInstruction {
        argRegs = List.copyOf(argRegs);
    }
}
