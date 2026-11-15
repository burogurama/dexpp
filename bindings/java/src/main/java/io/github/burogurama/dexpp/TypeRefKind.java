package io.github.burogurama.dexpp;

/**
 * What an instruction does with the type it names. The values match {@code DexppTypeRefKind} of the
 * C API.
 */
public enum TypeRefKind {
    /** {@code const-class}: loads the class object. */
    CONST_CLASS(0),
    /** {@code check-cast}: a cast to the type. */
    CHECK_CAST(1),
    /** {@code instance-of}: an {@code instanceof} test. */
    INSTANCE_OF(2),
    /** {@code new-instance}: creates an object. */
    NEW_INSTANCE(3),
    /** {@code new-array}. The type is the array type. */
    NEW_ARRAY(4),
    /** {@code filled-new-array}. The type is the array type. */
    FILLED_NEW_ARRAY(5);

    private final int value;

    TypeRefKind(int value) {
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
    public static TypeRefKind of(int value) {
        TypeRefKind[] all = values();
        if (value < 0 || value >= all.length || all[value].value != value) {
            throw new IllegalArgumentException("no dexpp TypeRefKind " + value);
        }
        return all[value];
    }
}
