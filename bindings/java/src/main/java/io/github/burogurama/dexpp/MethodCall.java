package io.github.burogurama.dexpp;

import java.util.Objects;

/**
 * One call in a method's code.
 *
 * @param codeOffset the offset of the invoke instruction, in code units
 * @param target the called method, exactly as the bytecode names it
 * @param kind the kind of invoke instruction
 */
public record MethodCall(int codeOffset, MethodId target, InvokeKind kind) {
    /** Checks the parts. */
    public MethodCall {
        Objects.requireNonNull(target, "target");
        Objects.requireNonNull(kind, "kind");
    }
}
