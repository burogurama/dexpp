package io.github.burogurama.dexpp;

/**
 * {@code const-method-type}: load a {@code java.lang.invoke.MethodType} into a register.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 * @param dest the register written
 * @param protoIndex the index of the prototype in the DEX file's prototype table
 */
public record ConstMethodTypeInstruction(int offset, int opcode, int size, int dest, int protoIndex)
        implements Instruction {}
