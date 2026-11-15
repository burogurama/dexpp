package io.github.burogurama.dexpp;

import java.util.Objects;

/**
 * One read or write of a field in a method's code.
 *
 * @param codeOffset the offset of the instruction, in code units
 * @param field the field that is read or written
 * @param isWrite true for a write, false for a read
 * @param isStatic true for a static field access
 */
public record FieldAccess(int codeOffset, FieldId field, boolean isWrite, boolean isStatic) {
    /** Checks that the field is given. */
    public FieldAccess {
        Objects.requireNonNull(field, "field");
    }
}
