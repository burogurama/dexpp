package io.github.burogurama.dexpp;

import java.lang.foreign.MemorySegment;
import java.util.Optional;

/**
 * A reference to a class by its descriptor. The class may not be in the loaded files: superclasses
 * and interfaces are often framework classes, which an app does not contain.
 *
 * <p>It is a handle: {@link #close()} frees it, and a {@link java.lang.ref.Cleaner} frees it when
 * it is no longer used. It keeps the analysis alive.
 */
public final class ClassRef implements AutoCloseable {
    private final NativeHandle handle;

    ClassRef(MemorySegment pointer) {
        handle = new NativeHandle("ClassRef", pointer, Native::dexpp_class_ref_free);
    }

    /** Frees the reference at once. A second call does nothing. */
    @Override
    public void close() {
        handle.close();
    }

    /**
     * The descriptor the reference names, such as {@code "Ljava/lang/Object;"}.
     *
     * @return the descriptor
     */
    public String descriptor() {
        return handle.call(p -> Mem.takeString(Native.dexpp_class_ref_descriptor(p)));
    }

    /**
     * Tells whether the class is defined in a loaded file.
     *
     * @return true when {@link #resolve()} finds the class
     */
    public boolean isResolved() {
        return handle.callBoolean(p -> Native.dexpp_class_ref_is_resolved(p) != 0);
    }

    /**
     * The referenced class.
     *
     * @return the class, or empty when no loaded file defines it
     */
    public Optional<DexClass> resolve() {
        return handle.call(p -> {
            MemorySegment cls = Native.dexpp_class_ref_resolve(p);
            return Mem.isNull(cls) ? Optional.empty() : Optional.of(new DexClass(cls));
        });
    }

    /** The descriptor; "closed" after {@link #close()}. */
    @Override
    public String toString() {
        try {
            return descriptor();
        } catch (IllegalStateException closed) {
            return "ClassRef (closed)";
        }
    }
}
