package io.github.burogurama.dexpp;

/**
 * A DEX file uses a format the editable {@link Model} does not support, such as the version 41
 * container format. Its code is {@link ErrorCode#UNSUPPORTED_DEX}.
 */
public class UnsupportedDexException extends DexppException {
    private static final long serialVersionUID = 1L;

    /**
     * Makes the exception.
     *
     * @param message what went wrong
     */
    public UnsupportedDexException(String message) {
        super(ErrorCode.UNSUPPORTED_DEX, message);
    }
}
