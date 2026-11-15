package io.github.burogurama.dexpp;

import java.util.List;
import java.util.Objects;

/**
 * The value of an annotation element, and the initial value of a static field ({@link
 * DexField#initialValue()}). It is one of the records nested here, or a nested {@link Annotation},
 * so a pattern-matching {@code switch} tells them apart:
 *
 * {@snippet :
 * String text = switch (value) {
 *     case AnnotationValue.IntValue(long number) -> "int " + number;
 *     case AnnotationValue.StringValue(String string) -> "string " + string;
 *     default -> value.toString();
 * };
 * }
 *
 * <p>Every integer type (byte, short, char, int, long) is an {@link IntValue}, and float and double
 * are a {@link FloatValue}: the original width of a number is not kept.
 */
public sealed interface AnnotationValue
        permits AnnotationValue.NullValue,
                AnnotationValue.BooleanValue,
                AnnotationValue.IntValue,
                AnnotationValue.FloatValue,
                AnnotationValue.StringValue,
                AnnotationValue.TypeRef,
                AnnotationValue.EnumRef,
                AnnotationValue.FieldRef,
                AnnotationValue.MethodRef,
                AnnotationValue.MethodTypeRef,
                AnnotationValue.MethodHandleRef,
                AnnotationValue.ArrayValue,
                Annotation {

    /** The null value. */
    NullValue NULL = new NullValue();

    /** A null value. */
    record NullValue() implements AnnotationValue {}

    /**
     * A {@code boolean}.
     *
     * @param value the value
     */
    record BooleanValue(boolean value) implements AnnotationValue {}

    /**
     * A {@code byte}, {@code short}, {@code char}, {@code int} or {@code long}.
     *
     * @param value the value, widened to {@code long}
     */
    record IntValue(long value) implements AnnotationValue {}

    /**
     * A {@code float} or a {@code double}.
     *
     * @param value the value, widened to {@code double}
     */
    record FloatValue(double value) implements AnnotationValue {}

    /**
     * A string. It may contain NUL characters.
     *
     * @param value the string
     */
    record StringValue(String value) implements AnnotationValue {
        /** Checks that the string is given. */
        public StringValue {
            Objects.requireNonNull(value, "value");
        }
    }

    /**
     * A class literal as a value, such as {@code String.class}.
     *
     * @param descriptor the type, such as {@code "Ljava/lang/String;"}
     */
    record TypeRef(String descriptor) implements AnnotationValue {
        /** Checks that the descriptor is given. */
        public TypeRef {
            Objects.requireNonNull(descriptor, "descriptor");
        }
    }

    /**
     * An enum constant as a value.
     *
     * @param constant the static field that holds the enum constant
     */
    record EnumRef(FieldId constant) implements AnnotationValue {
        /** Checks that the field is given. */
        public EnumRef {
            Objects.requireNonNull(constant, "constant");
        }
    }

    /**
     * A reference to a field as a value. It is rare outside system annotations.
     *
     * @param field the field
     */
    record FieldRef(FieldId field) implements AnnotationValue {
        /** Checks that the field is given. */
        public FieldRef {
            Objects.requireNonNull(field, "field");
        }
    }

    /**
     * A reference to a method as a value. It is rare outside system annotations.
     *
     * @param method the method
     */
    record MethodRef(MethodId method) implements AnnotationValue {
        /** Checks that the method is given. */
        public MethodRef {
            Objects.requireNonNull(method, "method");
        }
    }

    /**
     * A method prototype as a value.
     *
     * @param proto the prototype, such as {@code "(I)V"}
     */
    record MethodTypeRef(String proto) implements AnnotationValue {
        /** Checks that the prototype is given. */
        public MethodTypeRef {
            Objects.requireNonNull(proto, "proto");
        }
    }

    /**
     * A method handle as a value.
     *
     * @param index the position of the handle in the DEX file's method handle table; it is not
     *     resolved further
     */
    record MethodHandleRef(int index) implements AnnotationValue {}

    /**
     * An array of values.
     *
     * @param elements the elements, in order
     */
    record ArrayValue(List<AnnotationValue> elements) implements AnnotationValue {
        /** Copies the elements into an immutable list. */
        public ArrayValue {
            elements = List.copyOf(elements);
        }
    }
}
