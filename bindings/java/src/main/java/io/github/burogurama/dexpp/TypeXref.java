package io.github.burogurama.dexpp;

import java.util.Objects;

/**
 * One instruction that names a type, as {@link Xrefs} finds it.
 *
 * @param referrer the number of the method that holds the instruction; see {@link
 *     Xrefs#referrerId(int)}
 * @param codeOffset the offset of the instruction, in code units
 * @param kind what the instruction does with the type
 */
public record TypeXref(int referrer, int codeOffset, TypeUseKind kind) {
    /** Checks that the kind is given. */
    public TypeXref {
        Objects.requireNonNull(kind, "kind");
    }
}
