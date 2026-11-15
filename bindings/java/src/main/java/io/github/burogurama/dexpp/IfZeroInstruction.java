package io.github.burogurama.dexpp;

/**
 * {@code if-eqz}, {@code if-nez}, {@code if-ltz}, {@code if-gez}, {@code if-gtz} and {@code
 * if-lez}: compare one register with zero and jump when the condition holds.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 * @param branchOffset where the jump goes, counted from this instruction's own offset
 * @param reg the register compared with zero
 */
public record IfZeroInstruction(int offset, int opcode, int size, int branchOffset, int reg)
        implements Instruction {}
