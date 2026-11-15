package io.github.burogurama.dexpp;

/**
 * What a {@link CallNode} stands for. The values match {@code DexppCallNodeKind} of the C API.
 */
public enum CallNodeKind {
    /** A method defined in a loaded file. {@link CallGraph#methodFor(int)} gives its method. */
    DEFINED(0),
    /** A method that loaded code calls but no loaded file defines, such as a framework method. */
    EXTERNAL(1),
    /**
     * An {@code invoke-custom} call site. Its bootstrap method picks the target at runtime, so the
     * node stands for the call site itself. Its identity is opaque: it only keeps call sites apart
     * and names no method.
     */
    CALL_SITE(2);

    private final int value;

    CallNodeKind(int value) {
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
    public static CallNodeKind of(int value) {
        CallNodeKind[] all = values();
        if (value < 0 || value >= all.length || all[value].value != value) {
            throw new IllegalArgumentException("no dexpp CallNodeKind " + value);
        }
        return all[value];
    }
}
