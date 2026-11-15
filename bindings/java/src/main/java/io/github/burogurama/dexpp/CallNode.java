package io.github.burogurama.dexpp;

import java.util.Objects;

/**
 * A node of the {@link CallGraph}. Most nodes are methods: a method defined in a loaded file has
 * the kind {@link CallNodeKind#DEFINED}, and a method the program calls but does not contain, such
 * as a framework method, has the kind {@link CallNodeKind#EXTERNAL}. An {@code invoke-custom} call
 * site is a node of kind {@link CallNodeKind#CALL_SITE}.
 *
 * @param index the position of the node in the graph
 * @param id the method's identity; opaque for a {@link CallNodeKind#CALL_SITE} node
 * @param kind what the node stands for
 */
public record CallNode(int index, MethodId id, CallNodeKind kind) {
    /** Checks that the identity and the kind are given. */
    public CallNode {
        Objects.requireNonNull(id, "id");
        Objects.requireNonNull(kind, "kind");
    }
}
