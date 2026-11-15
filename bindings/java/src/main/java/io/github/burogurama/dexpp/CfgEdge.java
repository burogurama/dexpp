package io.github.burogurama.dexpp;

import java.util.Objects;
import java.util.OptionalLong;

/**
 * An edge from one basic block of a {@link Cfg} to another.
 *
 * @param targetBlock the index in {@link Cfg#blocks()} of the block the edge leads to
 * @param kind why execution can go along the edge
 * @param label extra data for two kinds of edge. For {@link CfgEdgeKind#SWITCH_CASE} it is the case
 *     key. For {@link CfgEdgeKind#EXCEPTION} it is the index of the caught type in the DEX file's
 *     type table, or {@code 0xFFFFFFFF} for a handler that catches everything. Empty for every
 *     other kind.
 */
public record CfgEdge(int targetBlock, CfgEdgeKind kind, OptionalLong label) {
    /** Checks the parts. */
    public CfgEdge {
        Objects.requireNonNull(kind, "kind");
        Objects.requireNonNull(label, "label");
    }
}
