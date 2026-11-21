package io.github.burogurama.dexpp;

/**
 * How a provider's {@link PathPermission} matches a path. Each value names the attribute of {@code
 * <path-permission>} that gives the path. The values match {@code DexppPathMatch} of the C API.
 */
public enum PathMatch {
    /** {@code android:path}: the whole path. */
    LITERAL(0),
    /** {@code android:pathPrefix}: the start of the path. */
    PREFIX(1),
    /** {@code android:pathPattern}: a simple glob. */
    PATTERN(2),
    /** {@code android:pathSuffix}: the end of the path. */
    SUFFIX(3),
    /** {@code android:pathAdvancedPattern}: an advanced glob. */
    ADVANCED_PATTERN(4);

    private final int value;

    PathMatch(int value) {
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
    public static PathMatch of(int value) {
        PathMatch[] all = values();
        if (value < 0 || value >= all.length || all[value].value != value) {
            throw new IllegalArgumentException("no dexpp PathMatch " + value);
        }
        return all[value];
    }
}
