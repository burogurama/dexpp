package io.github.burogurama.dexpp;

import java.util.List;

/**
 * {@code invoke-polymorphic} and {@code invoke-polymorphic/range}: call a signature-polymorphic
 * method, such as {@code MethodHandle.invokeExact}, with the prototype of this call.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 * @param methodIndex the index in the DEX file's method table, such as {@code
 *     MethodHandle.invokeExact}
 * @param protoIndex the index in the DEX file's prototype table of the prototype of this call: the
 *     types the call site passes and expects back
 * @param argRegs the argument registers, in order; the first one holds the method handle
 */
public record InvokePolymorphicInstruction(
        int offset, int opcode, int size, int methodIndex, int protoIndex, List<Integer> argRegs)
        implements Instruction {
    /** Checks the parts, and copies the list into an immutable list. */
    public InvokePolymorphicInstruction {
        argRegs = List.copyOf(argRegs);
    }
}
