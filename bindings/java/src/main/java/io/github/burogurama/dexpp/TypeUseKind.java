package io.github.burogurama.dexpp;

/**
 * What an instruction does with the type it names. The values match {@code DexppTypeUseKind} of the
 * C API.
 */
public enum TypeUseKind {
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

    TypeUseKind(int value) {
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
    public static TypeUseKind of(int value) {
        TypeUseKind[] all = values();
        if (value < 0 || value >= all.length || all[value].value != value) {
            throw new IllegalArgumentException("no dexpp TypeUseKind " + value);
        }
        return all[value];
    }
}
