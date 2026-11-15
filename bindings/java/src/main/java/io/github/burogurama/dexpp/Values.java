package io.github.burogurama.dexpp;

import static java.lang.foreign.ValueLayout.JAVA_DOUBLE;

import java.lang.foreign.MemorySegment;
import java.util.List;

/** Copies the annotation structs of the C API into Java records. */
final class Values {
    private Values() {}

    /** Copies the {@code DexppValue} at {@code base}. */
    static AnnotationValue value(MemorySegment m, long base) {
        int kind = Mem.i32(m, base + Layouts.Value.KIND);
        return switch (kind) {
            case 0 -> AnnotationValue.NULL;
            case 1 ->
                    new AnnotationValue.BooleanValue(Mem.bool(m, base + Layouts.Value.BOOL_VALUE));
            case 2 -> new AnnotationValue.IntValue(Mem.i64(m, base + Layouts.Value.INT_VALUE));
            case 3 ->
                    new AnnotationValue.FloatValue(
                            m.get(JAVA_DOUBLE, base + Layouts.Value.FLOAT_VALUE));
            case 4 ->
                    new AnnotationValue.StringValue(Mem.text(m, base + Layouts.Value.STRING_VALUE));
            case 5 ->
                    new AnnotationValue.TypeValue(
                            Mem.string(m, base + Layouts.Value.TYPE_DESCRIPTOR));
            case 6 -> new AnnotationValue.EnumValue(Mem.fieldId(m, base + Layouts.Value.FIELD));
            case 7 -> new AnnotationValue.FieldValue(Mem.fieldId(m, base + Layouts.Value.FIELD));
            case 8 ->
                    new AnnotationValue.MethodValue(Mem.methodId(m, base + Layouts.Value.METHOD));
            case 9 ->
                    new AnnotationValue.MethodTypeValue(
                            Mem.string(m, base + Layouts.Value.PROTO));
            case 10 -> {
                MethodHandleKind handle =
                        MethodHandleKind.of(Mem.i32(m, base + Layouts.Value.METHOD_HANDLE_KIND));
                MemberId member =
                        handle.worksOnField()
                                ? Mem.fieldId(m, base + Layouts.Value.FIELD)
                                : Mem.methodId(m, base + Layouts.Value.METHOD);
                yield new AnnotationValue.MethodHandleValue(handle, member);
            }
            case 11 ->
                    new AnnotationValue.ArrayValue(
                            Mem.array(
                                    Mem.pointer(m, base + Layouts.Value.ELEMENTS),
                                    Mem.i64(m, base + Layouts.Value.ELEMENT_COUNT),
                                    Layouts.Value.LAYOUT,
                                    Values::value));
            case 12 -> {
                MemorySegment nested = Mem.pointer(m, base + Layouts.Value.ANNOTATION);
                if (Mem.isNull(nested)) {
                    yield new Annotation("", AnnotationVisibility.BUILD, List.of());
                }
                yield annotation(nested.reinterpret(Layouts.Annotation.LAYOUT.byteSize()), 0);
            }
            // A kind this package does not know yet, from a newer library.
            default -> AnnotationValue.NULL;
        };
    }

    /** Copies the {@code DexppAnnotation} at {@code base}. */
    static Annotation annotation(MemorySegment m, long base) {
        List<AnnotationElement> elements =
                Mem.array(
                        Mem.pointer(m, base + Layouts.Annotation.ELEMENTS),
                        Mem.i64(m, base + Layouts.Annotation.ELEMENT_COUNT),
                        Layouts.AnnotationElement.LAYOUT,
                        (e, at) ->
                                new AnnotationElement(
                                        Mem.string(e, at + Layouts.AnnotationElement.NAME),
                                        value(e, at + Layouts.AnnotationElement.VALUE)));
        return new Annotation(
                Mem.string(m, base + Layouts.Annotation.TYPE_DESCRIPTOR),
                AnnotationVisibility.of(Mem.i32(m, base + Layouts.Annotation.VISIBILITY)),
                elements);
    }

    /** Calls a C function that returns an annotation array, copies it and frees it. */
    static List<Annotation> takeAnnotations(Mem.ArrayCall call) {
        return Mem.take(
                call,
                Layouts.Annotation.LAYOUT,
                Values::annotation,
                Native::dexpp_annotation_array_free);
    }
}
