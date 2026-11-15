package io.github.burogurama.dexpp;

/**
 * {@code nop}, which does nothing. Unused opcodes decode to it too.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 */
public record NopInstruction(int offset, int opcode, int size) implements Instruction {}
