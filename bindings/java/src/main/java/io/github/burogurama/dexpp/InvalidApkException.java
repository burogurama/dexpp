package io.github.burogurama.dexpp;

/**
 * The file is not a ZIP archive, or the APK has no {@code classes.dex}. Its code is {@link
 * ErrorCode#INVALID_APK}.
 */
public class InvalidApkException extends DexppException {
    private static final long serialVersionUID = 1L;

    /**
     * Makes the exception.
     *
     * @param message what went wrong
     */
    public InvalidApkException(String message) {
        super(ErrorCode.INVALID_APK, message);
    }
}
