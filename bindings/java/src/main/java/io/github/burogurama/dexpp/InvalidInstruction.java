package io.github.burogurama.dexpp;

/**
 * An opcode the Dalvik format does not use, as found in a damaged or deliberately broken method.
 * {@link #opcode()} is the raw byte and {@link #size()} is 1. Android rejects such a method, so it
 * never runs; the control-flow graph treats the instruction like a {@code nop}.
 *
 * @param offset the position of the instruction in the method, in 16-bit code units from the start
 *     of the code
 * @param opcode the raw opcode byte, from 0 to 255; {@link Dexpp#opcodeName} gives its name
 * @param size the length of the instruction in code units, from 1 to 5
 */
public record InvalidInstruction(int offset, int opcode, int size) implements Instruction {}
