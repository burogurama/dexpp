package io.github.burogurama.dexpp;

import java.util.Objects;

/**
 * One {@code name = value} pair of an {@link Annotation}.
 *
 * @param name the element name
 * @param value the element value
 */
public record AnnotationElement(String name, AnnotationValue value) {
    /** Checks that both parts are given. */
    public AnnotationElement {
        Objects.requireNonNull(name, "name");
        Objects.requireNonNull(value, "value");
    }
}
