package io.github.burogurama.dexpp;

import java.util.Objects;

/**
 * One typed catch clause of a {@link TryBlock}.
 *
 * @param typeDescriptor the exception class the clause catches
 * @param handlerOffset the offset of the handler's first instruction
 */
public record CatchHandler(String typeDescriptor, int handlerOffset) {
    /** Checks that the type is given. */
    public CatchHandler {
        Objects.requireNonNull(typeDescriptor, "typeDescriptor");
    }
}
