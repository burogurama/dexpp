package io.github.burogurama.dexpp;

import java.lang.foreign.MemorySegment;
import java.util.List;
import java.util.Optional;

/**
 * A class defined in a loaded DEX file.
 *
 * <p>It is a handle: {@link #close()} frees it, and a {@link java.lang.ref.Cleaner} frees it when
 * it is no longer used. It keeps the analysis alive, so it stays usable after its {@link
 * AnalysisContext} is closed.
 */
public final class DexClass implements AutoCloseable {
    private final NativeHandle handle;

    DexClass(MemorySegment pointer) {
        handle = new NativeHandle("DexClass", pointer, Native::dexpp_class_free);
    }

    /** Frees the class handle at once. A second call does nothing. */
    @Override
    public void close() {
        handle.close();
    }

    /**
     * The class descriptor, such as {@code "Lcom/example/Foo;"}. {@link DexMethod#name()} and
     * {@link DexField#name()} are simple names; a class is named by its descriptor.
     *
     * @return the descriptor
     */
    public String descriptor() {
        return handle.call(p -> Mem.takeString(Native.dexpp_class_descriptor(p)));
    }

    /**
     * The package in Java source form, with {@code .} between its parts, such as {@code
     * "com.example"}: the same form as {@link Manifest#packageName()}.
     *
     * @return the package; empty for a class in the default package
     */
    public String packageName() {
        return handle.call(p -> Mem.takeString(Native.dexpp_class_package(p)));
    }

    /**
     * The direct superclass.
     *
     * @return the superclass; empty only for {@code java.lang.Object} itself
     */
    public Optional<ClassRef> superclass() {
        return handle.call(p -> {
            MemorySegment ref = Native.dexpp_class_superclass(p);
            return Mem.isNull(ref) ? Optional.empty() : Optional.of(new ClassRef(ref));
        });
    }

    /**
     * The interfaces the class implements directly, in declaration order. For an interface, these
     * are the interfaces it extends.
     *
     * @return the interfaces
     */
    public List<ClassRef> interfaces() {
        return handle.call(p ->
                Mem.takeHandles(
                        (arena, count) -> Native.dexpp_class_interfaces(p, count),
                        ClassRef::new,
                        Native::dexpp_class_ref_array_free));
    }

    /**
     * The class's access flags.
     *
     * @return the flags
     */
    public AccessFlags accessFlags() {
        return new AccessFlags(handle.callInt(Native::dexpp_class_access_flags));
    }

    /**
     * Tells whether the class is an interface.
     *
     * @return true for an interface
     */
    public boolean isInterface() {
        return handle.callBoolean(p -> Native.dexpp_class_is_interface(p) != 0);
    }

    /**
     * Tells whether the class is abstract. Interfaces are abstract too.
     *
     * @return true for an abstract class or an interface
     */
    public boolean isAbstract() {
        return handle.callBoolean(p -> Native.dexpp_class_is_abstract(p) != 0);
    }

    /**
     * Tells whether the class is public.
     *
     * @return true for a public class
     */
    public boolean isPublic() {
        return accessFlags().has(AccessFlags.PUBLIC);
    }

    /**
     * Tells whether the class is final.
     *
     * @return true for a final class
     */
    public boolean isFinal() {
        return accessFlags().has(AccessFlags.FINAL);
    }

    /**
     * Tells whether the class is an enum.
     *
     * @return true for an enum
     */
    public boolean isEnum() {
        return accessFlags().has(AccessFlags.ENUM);
    }

    /**
     * The source file name recorded for the class, such as {@code "Foo.java"}.
     *
     * @return the file name, or empty when the DEX file records none
     */
    public Optional<String> sourceFile() {
        return handle.call(p -> Mem.takeOptString(Native.dexpp_class_source_file(p)));
    }

    /**
     * The methods the class defines: first the direct methods (constructors, static and private
     * methods), then the virtual methods. Inherited methods are not included.
     *
     * @return the methods
     */
    public List<DexMethod> methods() {
        return handle.call(p ->
                Mem.takeHandles(
                        (arena, count) -> Native.dexpp_class_methods(p, count),
                        DexMethod::new,
                        Native::dexpp_method_array_free));
    }

    /**
     * The fields the class declares: first the static fields, then the instance fields. Inherited
     * fields are not included.
     *
     * @return the fields
     */
    public List<DexField> fields() {
        return handle.call(p ->
                Mem.takeHandles(
                        (arena, count) -> Native.dexpp_class_fields(p, count),
                        DexField::new,
                        Native::dexpp_field_array_free));
    }

    /**
     * The annotations on the class.
     *
     * @return the annotations
     */
    public List<Annotation> annotations() {
        return handle.call(p ->
                Values.takeAnnotations((arena, count) -> Native.dexpp_class_annotations(p, count)));
    }

    /**
     * The class descriptor, such as {@code "Lcom/example/Foo;"}; "closed" after {@link #close()}.
     */
    @Override
    public String toString() {
        try {
            return descriptor();
        } catch (IllegalStateException closed) {
            return "DexClass (closed)";
        }
    }
}
