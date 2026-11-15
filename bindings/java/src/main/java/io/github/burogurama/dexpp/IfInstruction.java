package io.github.burogurama.dexpp;

/**
 * {@code if-eq}, {@code if-ne}, {@code if-lt}, {@code if-ge}, {@code if-gt} and {@code if-le}:
 * compare two registers and jump when the condition holds.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 * @param branchOffset where the jump goes, counted from this instruction's own offset
 * @param regA the first register compared
 * @param regB the second register compared
 */
public record IfInstruction(int offset, int opcode, int size, int branchOffset, int regA, int regB)
        implements Instruction {}
