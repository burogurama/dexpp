package io.github.burogurama.dexpp;

/**
 * A DEX file cannot be parsed: it is not a DEX file, or it is damaged. Its code is {@link
 * ErrorCode#INVALID_DEX}.
 */
public class InvalidDexException extends DexppException {
    private static final long serialVersionUID = 1L;

    /**
     * Makes the exception.
     *
     * @param message what went wrong
     */
    public InvalidDexException(String message) {
        super(ErrorCode.INVALID_DEX, message);
    }
}
