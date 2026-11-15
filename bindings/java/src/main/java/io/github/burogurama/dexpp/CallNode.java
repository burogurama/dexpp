package io.github.burogurama.dexpp;

import java.util.Objects;

/**
 * A method in the {@link CallGraph}. A node is resolved when the method is defined in a loaded
 * file. A method the program calls but does not contain, such as a framework method, is a node too,
 * with {@code resolved} false.
 *
 * @param index the position of the node in the graph
 * @param id the method's identity
 * @param resolved true when the method is defined in a loaded file
 */
public record CallNode(int index, MethodId id, boolean resolved) {
    /** Checks that the identity is given. */
    public CallNode {
        Objects.requireNonNull(id, "id");
    }
}
