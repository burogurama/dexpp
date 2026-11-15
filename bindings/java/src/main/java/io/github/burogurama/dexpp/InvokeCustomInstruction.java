package io.github.burogurama.dexpp;

import java.util.List;

/**
 * {@code invoke-custom} and {@code invoke-custom/range}: call through a call site that is bound at
 * run time.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 * @param callSiteIndex the index in the DEX file's call site table
 * @param argRegs the argument registers, in order
 */
public record InvokeCustomInstruction(
        int offset, int opcode, int size, int callSiteIndex, List<Integer> argRegs)
        implements Instruction {
    /** Checks the parts, and copies the lists into immutable lists. */
    public InvokeCustomInstruction {
        argRegs = List.copyOf(argRegs);
    }
}
