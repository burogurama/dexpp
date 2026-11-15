package io.github.burogurama.dexpp;

/**
 * The {@link Model} uses a feature that its DEX version does not have. Its code is {@link
 * ErrorCode#VERSION_TOO_LOW}.
 *
 * <p>Set the version to {@link Model#requiredVersion()}, or higher, with {@link Model#setVersion},
 * and write again.
 */
public class VersionTooLowException extends ModelException {
    private static final long serialVersionUID = 1L;

    /**
     * Makes the exception.
     *
     * @param message what went wrong
     */
    public VersionTooLowException(String message) {
        super(ErrorCode.VERSION_TOO_LOW, message);
    }
}
