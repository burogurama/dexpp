package io.github.burogurama.dexpp;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.List;
import java.util.Optional;

/**
 * A field declared by a class in a loaded DEX file.
 *
 * <p>It is a handle: {@link #close()} frees it, and a {@link java.lang.ref.Cleaner} frees it when
 * it is no longer used. It keeps the analysis alive.
 */
public final class DexField implements AutoCloseable {
    private final NativeHandle handle;

    DexField(MemorySegment pointer) {
        handle = new NativeHandle("DexField", pointer, Native::dexpp_field_free);
    }

    /** Frees the field handle at once. A second call does nothing. */
    @Override
    public void close() {
        handle.close();
    }

    /**
     * The field name.
     *
     * @return the name
     */
    public String name() {
        return handle.call(p -> Mem.takeString(Native.dexpp_field_name(p)));
    }

    /**
     * The class that declares the field.
     *
     * @return a reference to the class
     */
    public ClassRef declaringClass() {
        return handle.call(p -> new ClassRef(Native.dexpp_field_declaring_class(p)));
    }

    /**
     * The descriptor of the field's type, such as {@code "I"}.
     *
     * @return the type
     */
    public String type() {
        return handle.call(p -> Mem.takeString(Native.dexpp_field_type(p)));
    }

    /**
     * The field's identity: its class, name and type. It is the identity that the cross-references
     * use.
     *
     * @return the identity
     */
    public FieldId id() {
        try (ClassRef cls = declaringClass()) {
            return new FieldId(cls.descriptor(), name(), type());
        }
    }

    /**
     * The field's access flags.
     *
     * @return the flags
     */
    public AccessFlags accessFlags() {
        return new AccessFlags(handle.callInt(Native::dexpp_field_access_flags));
    }

    /**
     * Tells whether the field is public.
     *
     * @return true for a public field
     */
    public boolean isPublic() {
        return accessFlags().has(AccessFlags.PUBLIC);
    }

    /**
     * Tells whether the field is static.
     *
     * @return true for a static field
     */
    public boolean isStatic() {
        return accessFlags().has(AccessFlags.STATIC);
    }

    /**
     * Tells whether the field is final.
     *
     * @return true for a final field
     */
    public boolean isFinal() {
        return accessFlags().has(AccessFlags.FINAL);
    }

    /**
     * The annotations on the field.
     *
     * @return the annotations
     */
    public List<Annotation> annotations() {
        return handle.call(p ->
                Values.takeAnnotations((arena, count) -> Native.dexpp_field_annotations(p, count)));
    }

    /**
     * The constant a static field starts with.
     *
     * @return the value, or empty when the field is not static or the DEX file records no value for
     *     it; the field then starts at the default of its type: zero, false or null
     */
    public Optional<AnnotationValue> initialValue() {
        return handle.call(p -> {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment out = arena.allocate(Layouts.Value.LAYOUT);
                if (Native.dexpp_field_initial_value(p, out) == 0) {
                    return Optional.empty();
                }
                try {
                    return Optional.of(Values.value(out, 0));
                } finally {
                    Native.dexpp_value_clear(out);
                }
            }
        });
    }

    /** The identity, such as {@code "LFoo;->count:I"}; "closed" after {@link #close()}. */
    @Override
    public String toString() {
        try {
            return id().toString();
        } catch (IllegalStateException closed) {
            return "DexField (closed)";
        }
    }
}
