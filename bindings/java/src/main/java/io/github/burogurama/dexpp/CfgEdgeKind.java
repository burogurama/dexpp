package io.github.burogurama.dexpp;

/**
 * The kind of an edge between two basic blocks of a {@link Cfg}. The values match {@code
 * DexppCfgEdgeKind} of the C API.
 */
public enum CfgEdgeKind {
    /** Execution runs on into the next block. */
    FALLTHROUGH(0),
    /** An unconditional {@code goto}. */
    GOTO(1),
    /** The condition of an {@code if} holds. */
    BRANCH_TAKEN(2),
    /** The condition of an {@code if} does not hold. */
    BRANCH_NOT_TAKEN(3),
    /** One case of a switch. The label of the edge is the case key. */
    SWITCH_CASE(4),
    /** No case of a switch matches. */
    SWITCH_DEFAULT(5),
    /**
     * To a catch handler. The label of the edge is the index of the caught type in the DEX file's
     * type table, or {@code 0xFFFFFFFF} for a handler that catches everything.
     */
    EXCEPTION(6);

    private final int value;

    CfgEdgeKind(int value) {
        this.value = value;
    }

    /**
     * The number the C API uses for this value.
     *
     * @return the C number
     */
    public int value() {
        return value;
    }

    /**
     * The value with this C number.
     *
     * @param value a C number
     * @return the value
     * @throws IllegalArgumentException if no value has this number
     */
    public static CfgEdgeKind of(int value) {
        CfgEdgeKind[] all = values();
        if (value < 0 || value >= all.length || all[value].value != value) {
            throw new IllegalArgumentException("no dexpp CfgEdgeKind " + value);
        }
        return all[value];
    }
}
