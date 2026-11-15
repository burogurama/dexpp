package io.github.burogurama.dexpp;

import java.util.Objects;

/**
 * One {@code const-string} instruction in a method's code.
 *
 * @param codeOffset the offset of the instruction, in code units
 * @param value the string that is loaded; it may contain NUL characters
 */
public record StringLoad(int codeOffset, String value) {
    /** Checks that the string is given. */
    public StringLoad {
        Objects.requireNonNull(value, "value");
    }
}
