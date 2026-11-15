package io.github.burogurama.dexpp;

import java.util.Objects;

/**
 * The base class of the exceptions dex++ throws when it cannot use its input. It mirrors the Python
 * {@code dexpp.Error}.
 *
 * <p>It is unchecked. Catch it to handle every such error in one place, or catch a subclass for one
 * kind. {@link #code()} gives the {@link ErrorCode} of the C API.
 *
 * <p>A path that cannot be opened is not a {@code DexppException}. It is reported as an {@link
 * java.io.IOException}: a {@link java.nio.file.NoSuchFileException} when the file does not exist,
 * and a {@link java.nio.file.FileSystemException} for other file problems.
 *
 * <p>The base class itself is thrown for {@link ErrorCode#INTERNAL}: an unexpected failure, such as
 * running out of memory in C++.
 */
public class DexppException extends RuntimeException {
    private static final long serialVersionUID = 1L;

    /** The kind of error. */
    private final ErrorCode code;

    /**
     * Makes an exception.
     *
     * @param code the kind of error
     * @param message what went wrong
     */
    public DexppException(ErrorCode code, String message) {
        super(message);
        this.code = Objects.requireNonNull(code, "code");
    }

    /**
     * The kind of error, as the C API reports it.
     *
     * @return the error code
     */
    public ErrorCode code() {
        return code;
    }
}
