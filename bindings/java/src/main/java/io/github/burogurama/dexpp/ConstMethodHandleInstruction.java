package io.github.burogurama.dexpp;

/**
 * {@code const-method-handle}: load a {@code java.lang.invoke.MethodHandle} into a register.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 * @param dest the register written
 * @param methodHandleIndex the index of the handle in the DEX file's method handle table
 */
public record ConstMethodHandleInstruction(
        int offset, int opcode, int size, int dest, int methodHandleIndex)
        implements Instruction {}
