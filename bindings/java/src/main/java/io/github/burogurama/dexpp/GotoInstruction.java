package io.github.burogurama.dexpp;

/**
 * {@code goto}, {@code goto/16} and {@code goto/32}: an unconditional jump.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 * @param branchOffset where the jump goes, in code units, counted from this instruction's own
 *     offset; negative for a jump backwards
 */
public record GotoInstruction(int offset, int opcode, int size, int branchOffset)
        implements Instruction {}
