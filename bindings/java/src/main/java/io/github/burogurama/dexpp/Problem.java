package io.github.burogurama.dexpp;

import java.util.Objects;

/**
 * One rule of the DEX format that a {@link Model} breaks.
 *
 * @param where the place of the problem, such as {@code "Lcom/a/B;->run()V, item 12"}; empty for a
 *     problem with the whole file
 * @param message what is wrong
 */
public record Problem(String where, String message) {
    /** Checks that both parts are given. */
    public Problem {
        Objects.requireNonNull(where, "where");
        Objects.requireNonNull(message, "message");
    }
}
