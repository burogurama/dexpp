package io.github.burogurama.dexpp;

import java.util.Objects;

/**
 * One invoke instruction that calls a method, as {@link Xrefs} finds it.
 *
 * @param referrer the number of the method that holds the instruction; see {@link
 *     Xrefs#referrerId(int)}
 * @param codeOffset the offset of the instruction, in code units
 * @param kind the kind of invoke instruction
 */
public record MethodXref(int referrer, int codeOffset, InvokeKind kind) {
    /** Checks that the kind is given. */
    public MethodXref {
        Objects.requireNonNull(kind, "kind");
    }
}
