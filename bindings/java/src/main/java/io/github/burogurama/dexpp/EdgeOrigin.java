package io.github.burogurama.dexpp;

/**
 * How a call-graph edge came to point at its callee. The values match {@code DexppEdgeOrigin} of
 * the C API.
 */
public enum EdgeOrigin {
    /** The callee is the method the bytecode names. Every call site has exactly one such edge. */
    DECLARED(0),
    /**
     * The class the bytecode names does not define the method, and the callee is the nearest
     * definition in a loaded superclass or interface. A call site has at most one such edge.
     */
    INHERITED_RESOLUTION(1);

    private final int value;

    EdgeOrigin(int value) {
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
    public static EdgeOrigin of(int value) {
        EdgeOrigin[] all = values();
        if (value < 0 || value >= all.length || all[value].value != value) {
            throw new IllegalArgumentException("no dexpp EdgeOrigin " + value);
        }
        return all[value];
    }
}
