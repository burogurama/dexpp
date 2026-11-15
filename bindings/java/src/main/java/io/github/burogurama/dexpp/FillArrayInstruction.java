package io.github.burogurama.dexpp;

/**
 * {@code fill-array-data}: fill an array from a table of constants stored in the code.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 * @param reg the register that holds the array
 * @param payloadOffset where the table is stored, in code units, counted from this instruction's
 *     own offset
 */
public record FillArrayInstruction(int offset, int opcode, int size, int reg, int payloadOffset)
        implements Instruction {}
