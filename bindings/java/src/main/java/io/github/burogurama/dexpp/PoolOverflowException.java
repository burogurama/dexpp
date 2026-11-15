package io.github.burogurama.dexpp;

/**
 * A table of the written DEX file would have more entries than the format allows, for example more
 * than 65,536 methods. Its code is {@link ErrorCode#POOL_OVERFLOW}.
 *
 * <p>Move classes to another model and write two files. {@link Model#poolStats()} gives the table
 * sizes before writing.
 */
public class PoolOverflowException extends ModelException {
    private static final long serialVersionUID = 1L;

    /**
     * Makes the exception.
     *
     * @param message what went wrong
     */
    public PoolOverflowException(String message) {
        super(ErrorCode.POOL_OVERFLOW, message);
    }
}
