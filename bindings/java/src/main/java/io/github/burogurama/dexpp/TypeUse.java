package io.github.burogurama.dexpp;

import java.util.Objects;

/**
 * One instruction in a method's code that names a type: {@code const-class}, {@code check-cast},
 * {@code instance-of}, {@code new-instance}, {@code new-array} or {@code filled-new-array}.
 *
 * @param codeOffset the offset of the instruction, in code units
 * @param descriptor the type the instruction names
 * @param kind what the instruction does with the type
 */
public record TypeUse(int codeOffset, String descriptor, TypeRefKind kind) {
    /** Checks the parts. */
    public TypeUse {
        Objects.requireNonNull(descriptor, "descriptor");
        Objects.requireNonNull(kind, "kind");
    }
}
