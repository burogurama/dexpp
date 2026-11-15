package io.github.burogurama.dexpp;

/**
 * {@code monitor-enter} and {@code monitor-exit}: take and release the lock of an object, at the
 * start and end of a synchronized block.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 * @param reg the register that holds the object
 * @param isEnter true for {@code monitor-enter}, false for {@code monitor-exit}
 */
public record MonitorInstruction(int offset, int opcode, int size, int reg, boolean isEnter)
        implements Instruction {}
