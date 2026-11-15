package io.github.burogurama.dexpp;

/**
 * The kind of a component declared in the manifest. The values match {@code DexppComponentKind} of
 * the C API.
 */
public enum ComponentKind {
    /** An {@code <activity>}: a screen of the app. */
    ACTIVITY(0),
    /** A {@code <service>}: work without a screen. */
    SERVICE(1),
    /** A {@code <receiver>}: a broadcast receiver. */
    RECEIVER(2),
    /** A {@code <provider>}: a content provider. */
    PROVIDER(3);

    private final int value;

    ComponentKind(int value) {
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
    public static ComponentKind of(int value) {
        ComponentKind[] all = values();
        if (value < 0 || value >= all.length || all[value].value != value) {
            throw new IllegalArgumentException("no dexpp ComponentKind " + value);
        }
        return all[value];
    }
}
