package io.github.burogurama.dexpp;

/**
 * {@code const-method-handle} and {@code const-method-type}.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 * @param dest the register written
 * @param index an index into the method handle table for {@code const-method-handle}, or into the
 *     prototype table for {@code const-method-type}
 */
public record ConstHandleInstruction(int offset, int opcode, int size, int dest, int index)
        implements Instruction {}
