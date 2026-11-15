package io.github.burogurama.dexpp;

import java.util.Objects;

/**
 * One call site in the {@link CallGraph}: an invoke instruction in a caller, pointing at a callee.
 * A call site has one {@link EdgeOrigin#DECLARED} edge, and a second, {@link
 * EdgeOrigin#INHERITED_RESOLUTION} edge when the class the bytecode names does not define the
 * method itself. Both edges have the same caller, kind and offset.
 *
 * @param index the position of the edge in the graph
 * @param callerNode the index of the calling method's node
 * @param calleeNode the index of the called method's node
 * @param kind the kind of invoke instruction
 * @param codeOffset the offset of the invoke instruction in the caller's code
 * @param origin the declared target, or the one found by walking up the hierarchy
 */
public record CallEdge(
        int index,
        int callerNode,
        int calleeNode,
        InvokeKind kind,
        int codeOffset,
        EdgeOrigin origin) {
    /** Checks the parts. */
    public CallEdge {
        Objects.requireNonNull(kind, "kind");
        Objects.requireNonNull(origin, "origin");
    }
}
