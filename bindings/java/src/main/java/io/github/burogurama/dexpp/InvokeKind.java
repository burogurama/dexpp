package io.github.burogurama.dexpp;

/**
 * The kind of invoke instruction at a call site. The values match {@code DexppInvokeKind} of the C
 * API.
 */
public enum InvokeKind {
    /** {@code invoke-virtual}. */
    VIRTUAL(0),
    /** {@code invoke-super}. */
    SUPER(1),
    /** {@code invoke-direct}: constructors and private methods. */
    DIRECT(2),
    /** {@code invoke-static}. */
    STATIC(3),
    /** {@code invoke-interface}. */
    INTERFACE(4),
    /** {@code invoke-polymorphic}: a call through a method handle. */
    POLYMORPHIC(5),
    /** {@code invoke-custom}: a call site that is bound at run time. */
    CUSTOM(6);

    private final int value;

    InvokeKind(int value) {
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
    public static InvokeKind of(int value) {
        InvokeKind[] all = values();
        if (value < 0 || value >= all.length || all[value].value != value) {
            throw new IllegalArgumentException("no dexpp InvokeKind " + value);
        }
        return all[value];
    }
}
