package io.github.burogurama.dexpp;

/** How many registers a value takes. The values match {@code DexppOperandWidth} of the C API. */
public enum OperandWidth {
    /** A 32-bit value or a reference: one register. */
    SINGLE(0),
    /** A {@code long} or a {@code double}: two registers, vN and vN+1. */
    PAIR(1);

    private final int value;

    OperandWidth(int value) {
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
    public static OperandWidth of(int value) {
        OperandWidth[] all = values();
        if (value < 0 || value >= all.length || all[value].value != value) {
            throw new IllegalArgumentException("no dexpp OperandWidth " + value);
        }
        return all[value];
    }
}
