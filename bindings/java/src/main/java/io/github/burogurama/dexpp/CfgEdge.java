package io.github.burogurama.dexpp;

import java.util.Objects;
import java.util.Optional;
import java.util.OptionalInt;

/**
 * An edge from one basic block of a {@link Cfg} to another.
 *
 * @param targetBlock the index in {@link Cfg#blocks()} of the block the edge leads to
 * @param kind why execution can go along the edge
 * @param caseKey the value the switch case matches, for a {@link CfgEdgeKind#SWITCH_CASE} edge;
 *     empty for every other kind
 * @param catchType the descriptor of the exception class the handler catches, such as {@code
 *     "Ljava/io/IOException;"}, for a {@link CfgEdgeKind#EXCEPTION} edge; empty for every other
 *     kind, including {@link CfgEdgeKind#EXCEPTION_CATCH_ALL}, which catches everything
 */
public record CfgEdge(
        int targetBlock, CfgEdgeKind kind, OptionalInt caseKey, Optional<String> catchType) {
    /** Checks the parts. */
    public CfgEdge {
        Objects.requireNonNull(kind, "kind");
        Objects.requireNonNull(caseKey, "caseKey");
        Objects.requireNonNull(catchType, "catchType");
    }
}
