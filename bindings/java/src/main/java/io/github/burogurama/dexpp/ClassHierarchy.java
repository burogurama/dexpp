package io.github.burogurama.dexpp;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.List;
import java.util.Optional;

/**
 * Inheritance over the whole program. Get it from {@link AnalysisContext#classHierarchy()}.
 *
 * <p>Every query takes and returns class descriptors. A class outside the loaded files is in the
 * hierarchy when a loaded class names it as its superclass or as an interface; its own ancestors
 * are not known. A query about a descriptor the hierarchy has never seen gives an empty result or
 * false.
 *
 * <p>It is a handle: {@link #close()} frees it, and a {@link java.lang.ref.Cleaner} frees it when
 * it is no longer used. The hierarchy itself stays in the analysis.
 */
public final class ClassHierarchy implements AutoCloseable {
    private final NativeHandle handle;

    ClassHierarchy(MemorySegment pointer) {
        handle = new NativeHandle("ClassHierarchy", pointer, Native::dexpp_class_hierarchy_free);
    }

    /** Frees the handle at once. A second call does nothing. */
    @Override
    public void close() {
        handle.close();
    }

    /** A C query that takes a descriptor and returns an array of descriptors. */
    @FunctionalInterface
    private interface Query {
        MemorySegment run(MemorySegment hierarchy, MemorySegment descriptor, MemorySegment count);
    }

    private List<String> query(String descriptor, Query query) {
        return handle.call(h ->
                Mem.takeStrings(
                        (arena, count) -> query.run(h, Mem.cString(arena, descriptor), count)));
    }

    /**
     * The direct superclass, then the directly implemented interfaces.
     *
     * @param descriptor a class descriptor
     * @return the descriptors
     */
    public List<String> supertypes(String descriptor) {
        return query(descriptor, Native::dexpp_class_hierarchy_supertypes);
    }

    /**
     * The direct superclass.
     *
     * @param descriptor a class descriptor
     * @return the superclass, or empty when none is recorded: for {@code java.lang.Object}, for a
     *     class outside the loaded files, and for a descriptor the hierarchy does not know
     */
    public Optional<String> superclassOf(String descriptor) {
        return handle.call(h -> {
            try (Arena arena = Arena.ofConfined()) {
                return Mem.takeOptString(
                        Native.dexpp_class_hierarchy_superclass_of(
                                h, Mem.cString(arena, descriptor)));
            }
        });
    }

    /**
     * The interfaces the class implements directly, or, for an interface, the interfaces it extends
     * directly.
     *
     * @param descriptor a class descriptor
     * @return the descriptors
     */
    public List<String> directInterfaces(String descriptor) {
        return query(descriptor, Native::dexpp_class_hierarchy_direct_interfaces);
    }

    /**
     * The classes that directly extend the class.
     *
     * @param descriptor a class descriptor
     * @return the descriptors
     */
    public List<String> subclasses(String descriptor) {
        return query(descriptor, Native::dexpp_class_hierarchy_subclasses);
    }

    /**
     * The classes that directly implement the interface.
     *
     * @param descriptor an interface descriptor
     * @return the descriptors
     */
    public List<String> implementers(String descriptor) {
        return query(descriptor, Native::dexpp_class_hierarchy_implementers);
    }

    /**
     * Every ancestor: superclasses and interfaces, at any distance. The class itself is not
     * included.
     *
     * @param descriptor a class descriptor
     * @return the descriptors
     */
    public List<String> allSupertypes(String descriptor) {
        return query(descriptor, Native::dexpp_class_hierarchy_all_supertypes);
    }

    /**
     * Every class below this one, at any distance, through both {@code extends} and {@code
     * implements}. The class itself is not included.
     *
     * @param descriptor a class descriptor
     * @return the descriptors
     */
    public List<String> allDescendants(String descriptor) {
        return query(descriptor, Native::dexpp_class_hierarchy_all_descendants);
    }

    /**
     * Tells whether {@code sub} is {@code sup} itself or has {@code sup} among its ancestors.
     *
     * @param sub a class descriptor
     * @param sup a class descriptor
     * @return true when {@code sub} is a subtype of {@code sup}
     */
    public boolean isSubtypeOf(String sub, String sup) {
        return handle.callBoolean(h -> {
            try (Arena arena = Arena.ofConfined()) {
                return Native.dexpp_class_hierarchy_is_subtype_of(
                                h, Mem.cString(arena, sub), Mem.cString(arena, sup))
                        != 0;
            }
        });
    }

    /**
     * Tells whether the class is defined in a loaded file.
     *
     * @param descriptor a class descriptor
     * @return true for a loaded class
     */
    public boolean isLoaded(String descriptor) {
        return handle.callBoolean(h -> {
            try (Arena arena = Arena.ofConfined()) {
                return Native.dexpp_class_hierarchy_is_loaded(h, Mem.cString(arena, descriptor))
                        != 0;
            }
        });
    }

    /**
     * Tells whether the class is defined in a loaded file and is an interface. It is false for a
     * class outside the loaded files, because its kind is not known.
     *
     * @param descriptor a class descriptor
     * @return true for a loaded interface
     */
    public boolean isInterface(String descriptor) {
        return handle.callBoolean(h -> {
            try (Arena arena = Arena.ofConfined()) {
                return Native.dexpp_class_hierarchy_is_interface(h, Mem.cString(arena, descriptor))
                        != 0;
            }
        });
    }

    /**
     * Tells whether the hierarchy has no classes.
     *
     * @return true for an empty hierarchy
     */
    public boolean isEmpty() {
        return handle.callBoolean(h -> Native.dexpp_class_hierarchy_empty(h) != 0);
    }
}
