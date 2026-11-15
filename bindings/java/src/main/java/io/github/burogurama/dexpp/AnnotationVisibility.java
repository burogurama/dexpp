package io.github.burogurama.dexpp;

/** Who can see an annotation. The values match {@code DexppAnnotationVisibility} of the C API. */
public enum AnnotationVisibility {
    /** Only tools that read the file. The running app does not see the annotation. */
    BUILD(0),
    /** The running app can read the annotation through reflection. */
    RUNTIME(1),
    /** Data the compiler writes for the runtime, in the {@code dalvik.annotation.*} annotations. */
    SYSTEM(2);

    private final int value;

    AnnotationVisibility(int value) {
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
    public static AnnotationVisibility of(int value) {
        AnnotationVisibility[] all = values();
        if (value < 0 || value >= all.length || all[value].value != value) {
            throw new IllegalArgumentException("no dexpp AnnotationVisibility " + value);
        }
        return all[value];
    }
}
