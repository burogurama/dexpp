package io.github.burogurama.dexpp;

import java.util.List;
import java.util.Objects;
import java.util.Optional;

/**
 * One annotation on a class, a method or a field. It is a copy of the data, so it stays valid on
 * its own. It is also an {@link AnnotationValue}: the value of an element can be a nested
 * annotation.
 *
 * @param typeDescriptor the annotation's class, such as {@code "Ljava/lang/Deprecated;"}
 * @param visibility who can see the annotation
 * @param elements the elements the source sets, in DEX order (sorted by name). An element left at
 *     its default value is not listed: the DEX format stores defaults once, on the annotation's own
 *     class.
 */
public record Annotation(
        String typeDescriptor, AnnotationVisibility visibility, List<AnnotationElement> elements)
        implements AnnotationValue {
    /** Checks the parts, and copies the elements into an immutable list. */
    public Annotation {
        Objects.requireNonNull(typeDescriptor, "typeDescriptor");
        Objects.requireNonNull(visibility, "visibility");
        elements = List.copyOf(elements);
    }

    /**
     * The value of the element with this name.
     *
     * @param name the element name
     * @return the value, or empty when the annotation does not list the element, which is also the
     *     case for an element left at its default value
     */
    public Optional<AnnotationValue> element(String name) {
        for (AnnotationElement e : elements) {
            if (e.name().equals(name)) {
                return Optional.of(e.value());
            }
        }
        return Optional.empty();
    }
}
