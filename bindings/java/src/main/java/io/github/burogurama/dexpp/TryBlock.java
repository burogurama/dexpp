package io.github.burogurama.dexpp;

import java.util.List;
import java.util.Objects;
import java.util.OptionalInt;

/**
 * A range of a method's code that is covered by exception handlers.
 *
 * @param startOffset the offset of the first covered instruction
 * @param endOffset the offset just past the covered range
 * @param handlers the typed catch clauses, in the order they are tried
 * @param catchAllOffset the offset of the handler that catches every exception, or empty when there
 *     is none. A {@code finally} block is compiled to such a handler.
 */
public record TryBlock(
        int startOffset, int endOffset, List<CatchHandler> handlers, OptionalInt catchAllOffset) {
    /** Checks the parts, and copies the handlers into an immutable list. */
    public TryBlock {
        handlers = List.copyOf(handlers);
        Objects.requireNonNull(catchAllOffset, "catchAllOffset");
    }

    /**
     * Tells whether a handler catches every exception.
     *
     * @return true when {@link #catchAllOffset()} is present
     */
    public boolean hasCatchAll() {
        return catchAllOffset.isPresent();
    }
}
