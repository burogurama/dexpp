package io.github.burogurama.dexpp;

import java.util.List;

/**
 * A {@link Model} breaks rules of the DEX format, so it cannot be written. Its code is {@link
 * ErrorCode#INVALID_MODEL}.
 *
 * <p>{@link #problems()} lists every broken rule. {@link Model#validate()} returns the same list
 * without throwing.
 */
public class InvalidModelException extends ModelException {
    private static final long serialVersionUID = 1L;

    /** Every broken rule. An immutable list, so it can be serialized. */
    @SuppressWarnings("serial")
    private final List<Problem> problems;

    /**
     * Makes the exception.
     *
     * @param message a summary of what is wrong
     * @param problems the broken rules
     */
    public InvalidModelException(String message, List<Problem> problems) {
        super(ErrorCode.INVALID_MODEL, message);
        this.problems = List.copyOf(problems);
    }

    /**
     * Every rule of the DEX format the model breaks, with its place.
     *
     * @return the problems, in an immutable list
     */
    public List<Problem> problems() {
        return problems;
    }
}
