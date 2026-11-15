package io.github.burogurama.dexpp;

/**
 * Arithmetic on a register and a constant: {@code add-int/lit16}, {@code rsub-int}, {@code
 * mul-int/lit8}, {@code ushr-int/lit8} and the like.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 * @param dest the register written
 * @param src the register read
 * @param literal the constant operand
 */
public record BinaryLitInstruction(int offset, int opcode, int size, int dest, int src, int literal)
        implements Instruction {}
