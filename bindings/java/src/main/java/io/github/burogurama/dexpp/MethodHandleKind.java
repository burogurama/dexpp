package io.github.burogurama.dexpp;

/**
 * What a method handle does. The values are the method handle type codes of the DEX format, and
 * match {@code DexppMethodHandleKind} of the C API. The first four work on a field, the other five
 * call a method.
 */
public enum MethodHandleKind {
    /** Writes a static field. */
    STATIC_PUT(0),
    /** Reads a static field. */
    STATIC_GET(1),
    /** Writes an instance field. */
    INSTANCE_PUT(2),
    /** Reads an instance field. */
    INSTANCE_GET(3),
    /** Calls a static method. */
    INVOKE_STATIC(4),
    /** Calls an instance method, with virtual dispatch. */
    INVOKE_INSTANCE(5),
    /** Creates an object with a constructor. */
    INVOKE_CONSTRUCTOR(6),
    /** Calls an instance method without dispatch, such as a private method. */
    INVOKE_DIRECT(7),
    /** Calls an interface method. */
    INVOKE_INTERFACE(8);

    private final int value;

    MethodHandleKind(int value) {
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

    /** True for the four kinds that read or write a field. */
    boolean worksOnField() {
        return this == STATIC_PUT
                || this == STATIC_GET
                || this == INSTANCE_PUT
                || this == INSTANCE_GET;
    }

    /**
     * The value with this C number.
     *
     * @param value a C number
     * @return the value
     * @throws IllegalArgumentException if no value has this number
     */
    public static MethodHandleKind of(int value) {
        MethodHandleKind[] all = values();
        if (value < 0 || value >= all.length || all[value].value != value) {
            throw new IllegalArgumentException("no dexpp MethodHandleKind " + value);
        }
        return all[value];
    }
}
