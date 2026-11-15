package io.github.burogurama.dexpp;

/**
 * {@code throw}: throws the exception held in a register.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 * @param reg the register that holds the exception
 */
public record ThrowInstruction(int offset, int opcode, int size, int reg) implements Instruction {}
