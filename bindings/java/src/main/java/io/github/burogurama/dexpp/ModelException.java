package io.github.burogurama.dexpp;

/**
 * An operation on a {@link Model} failed.
 *
 * <p>{@link Model#copyClass} and {@link Model#moveClass} throw it directly, with the code {@link
 * ErrorCode#MODEL}, when the source model has no such class or the target model already has one. It
 * is also the base class of the exceptions {@link Model#write()} throws: {@link
 * InvalidModelException}, {@link PoolOverflowException} and {@link VersionTooLowException}.
 */
public class ModelException extends DexppException {
    private static final long serialVersionUID = 1L;

    /**
     * Makes the exception, with the code {@link ErrorCode#MODEL}.
     *
     * @param message what went wrong
     */
    public ModelException(String message) {
        super(ErrorCode.MODEL, message);
    }

    /**
     * Makes the exception of a subclass.
     *
     * @param code the code of the subclass
     * @param message what went wrong
     */
    protected ModelException(ErrorCode code, String message) {
        super(code, message);
    }
}
