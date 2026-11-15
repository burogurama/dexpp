package io.github.burogurama.dexpp;

/**
 * What went wrong in a failed call. The values match {@code DexppErrorCode} of the C API, and each
 * one has an exception of the same name in the Python binding.
 *
 * <p>The three file codes are reported as the JDK's file exceptions, which are {@link
 * java.io.IOException}s: {@link #FILE_NOT_FOUND} as a {@link java.nio.file.NoSuchFileException},
 * {@link #CANNOT_READ} and {@link #CANNOT_WRITE} as a {@link java.nio.file.FileSystemException}.
 * Every other code is reported as a {@link DexppException}, whose {@link DexppException#code()}
 * gives the code.
 */
public enum ErrorCode {
    /** The path does not exist. */
    FILE_NOT_FOUND(1),
    /** The path exists but cannot be read, for example because it is a folder. */
    CANNOT_READ(2),
    /** The file cannot be created or written. */
    CANNOT_WRITE(3),
    /** The file is not a ZIP archive, or the APK has no {@code classes.dex}. */
    INVALID_APK(4),
    /** A DEX file cannot be parsed. */
    INVALID_DEX(5),
    /** A DEX format the model does not read, such as version 41. */
    UNSUPPORTED_DEX(6),
    /** A model breaks rules of the DEX format. */
    INVALID_MODEL(7),
    /** A table of the written DEX file would have more entries than the format allows. */
    POOL_OVERFLOW(8),
    /** A model uses a feature that its DEX version does not have. */
    VERSION_TOO_LOW(9),
    /** A class to move or copy is missing from the source model, or already in the target. */
    MODEL(10),
    /** An unexpected failure, such as running out of memory. */
    INTERNAL(11);

    private final int value;

    ErrorCode(int value) {
        this.value = value;
    }

    /**
     * The number the C API uses for this code.
     *
     * @return the C number
     */
    public int value() {
        return value;
    }

    /**
     * The code with this C number.
     *
     * @param value a C number
     * @return the code
     * @throws IllegalArgumentException if no code has this number
     */
    public static ErrorCode of(int value) {
        for (ErrorCode code : values()) {
            if (code.value == value) {
                return code;
            }
        }
        throw new IllegalArgumentException("no dexpp error code " + value);
    }
}
