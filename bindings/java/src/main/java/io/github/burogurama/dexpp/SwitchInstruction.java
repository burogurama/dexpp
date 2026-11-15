package io.github.burogurama.dexpp;

import java.util.List;

/**
 * {@code packed-switch} and {@code sparse-switch}: jump to the case that matches a register.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 * @param payloadOffset where the case table is stored, in code units, counted from this
 *     instruction's own offset
 * @param reg the register that is tested
 * @param isPacked true for {@code packed-switch}, false for {@code sparse-switch}
 * @param cases the decoded cases; empty when the case table is damaged
 */
public record SwitchInstruction(
        int offset,
        int opcode,
        int size,
        int payloadOffset,
        int reg,
        boolean isPacked,
        List<SwitchCase> cases)
        implements Instruction {
    /** Checks the parts, and copies the lists into immutable lists. */
    public SwitchInstruction {
        cases = List.copyOf(cases);
    }
}
