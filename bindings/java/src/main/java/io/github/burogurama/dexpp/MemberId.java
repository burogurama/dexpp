package io.github.burogurama.dexpp;

/**
 * The identity of a method or a field: a {@link MethodId} or a {@link FieldId}. {@link
 * AnnotationValue.MethodHandleValue#member()} gives one, and a pattern-matching {@code switch}
 * tells the two apart:
 *
 * {@snippet :
 * String text = switch (member) {
 *     case MethodId method -> "calls " + method;
 *     case FieldId field -> "uses " + field;
 * };
 * }
 */
public sealed interface MemberId permits MethodId, FieldId {}
