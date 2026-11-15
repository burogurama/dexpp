package io.github.burogurama.dexpp;

import java.util.Objects;

/**
 * An incoming argument of a method and the register it arrives in. Dalvik puts the arguments in the
 * last registers of the frame.
 *
 * @param reg the register; a wide argument also uses {@code reg + 1}
 * @param type the argument's type descriptor; the declaring class for {@code this}
 * @param isThis true for the implicit {@code this} of an instance method
 * @param isWide true for {@code long} and {@code double}, which take two registers
 */
public record ParameterRegister(int reg, String type, boolean isThis, boolean isWide) {
    /** Checks that the type is given. */
    public ParameterRegister {
        Objects.requireNonNull(type, "type");
    }
}
