package io.github.burogurama.dexpp;

/**
 * {@code array-length}: store the length of an array in a register.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 * @param dest the register that gets the length
 * @param arrayReg the register that holds the array
 */
public record ArrayLengthInstruction(int offset, int opcode, int size, int dest, int arrayReg)
        implements Instruction {}
