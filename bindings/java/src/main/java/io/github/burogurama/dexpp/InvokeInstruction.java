package io.github.burogurama.dexpp;

import java.util.List;

/**
 * {@code invoke-virtual}, {@code invoke-super}, {@code invoke-direct}, {@code invoke-static},
 * {@code invoke-interface} and their {@code /range} forms, and {@code invoke-polymorphic}: call a
 * method.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 * @param methodIndex the index in the DEX file's method table
 * @param argRegs the argument registers, in order. For a call on an object the first one holds the
 *     object. A {@code long} or {@code double} argument takes two entries
 */
public record InvokeInstruction(
        int offset, int opcode, int size, int methodIndex, List<Integer> argRegs)
        implements Instruction {
    /** Checks the parts, and copies the lists into immutable lists. */
    public InvokeInstruction {
        argRegs = List.copyOf(argRegs);
    }
}
