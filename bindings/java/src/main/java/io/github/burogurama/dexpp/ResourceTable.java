package io.github.burogurama.dexpp;

import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.Optional;
import java.util.OptionalInt;
import java.util.OptionalLong;

/**
 * The decoded {@code resources.arsc} table of an {@link Apk}. It maps resource ids to names and to
 * string, boolean and integer values. When a resource has several variants, for example one per
 * language, the value of the default configuration is used. A resource whose value refers to
 * another resource is followed to the end.
 *
 * <p>It reads through the {@link Apk} it came from, so that APK must stay open while the table is
 * used.
 */
public final class ResourceTable {
    private final Apk apk;

    ResourceTable(Apk apk) {
        this.apk = apk;
    }

    /**
     * Tells whether the table has no entries.
     *
     * @return true for an empty table
     */
    public boolean isEmpty() {
        return apk.handle().callBoolean(a -> Native.dexpp_apk_resources_empty(a) != 0);
    }

    /**
     * The name of the resource with this id.
     *
     * @param id a resource id, such as {@code 0x7f010000}
     * @return the name, or empty when the table has no such id
     */
    public Optional<ResourceName> nameOf(int id) {
        return apk.handle()
                .call(a -> {
                    try (Arena arena = Arena.ofConfined()) {
                        MemorySegment out = arena.allocate(Layouts.ResourceName.LAYOUT);
                        if (Native.dexpp_apk_resource_name(a, id, out) == 0) {
                            return Optional.empty();
                        }
                        try {
                            return Optional.of(
                                    new ResourceName(
                                            Mem.string(out, Layouts.ResourceName.PACKAGE),
                                            Mem.string(out, Layouts.ResourceName.TYPE),
                                            Mem.string(out, Layouts.ResourceName.ENTRY)));
                        } finally {
                            Native.dexpp_resource_name_clear(out);
                        }
                    }
                });
    }

    /**
     * The value of a string resource. The same as {@link Apk#resolveString}.
     *
     * @param id a resource id
     * @return the string, or empty when the id is missing or its value is not a string
     */
    public Optional<String> resolveString(int id) {
        return apk.resolveString(id);
    }

    /**
     * The value of a resource as a boolean: a bool resource, or any integer-typed value (nonzero is
     * true), as Android reads it.
     *
     * @param id a resource id
     * @return the value, or empty when the id is missing or its value is of another type, such as a
     *     string
     */
    public Optional<Boolean> resolveBool(int id) {
        return apk.handle()
                .call(a -> {
                    try (Arena arena = Arena.ofConfined()) {
                        MemorySegment out = arena.allocate(JAVA_INT);
                        if (Native.dexpp_apk_resolve_bool(a, id, out) == 0) {
                            return Optional.empty();
                        }
                        return Optional.of(out.get(JAVA_INT, 0) != 0);
                    }
                });
    }

    /**
     * The value of a resource as an integer: an integer resource, or any other integer-typed value,
     * read as its signed 32-bit data.
     *
     * @param id a resource id
     * @return the value, or empty when the id is missing or its value is of another type
     */
    public OptionalLong resolveInt(int id) {
        return apk.handle()
                .call(a -> {
                    try (Arena arena = Arena.ofConfined()) {
                        MemorySegment out = arena.allocate(JAVA_LONG);
                        if (Native.dexpp_apk_resolve_int(a, id, out) == 0) {
                            return OptionalLong.empty();
                        }
                        return OptionalLong.of(out.get(JAVA_LONG, 0));
                    }
                });
    }

    /**
     * The id of the resource with this type and entry name. It scans the table, so it is meant for
     * single lookups, not for loops.
     *
     * @param type a resource type, such as {@code "string"}
     * @param entry an entry name, such as {@code "app_name"}
     * @return the id, or empty when there is none
     */
    public OptionalInt idOf(String type, String entry) {
        return apk.handle()
                .call(a -> {
                    try (Arena arena = Arena.ofConfined()) {
                        MemorySegment out = arena.allocate(JAVA_INT);
                        if (Native.dexpp_apk_resource_id(
                                        a, Mem.cString(arena, type), Mem.cString(arena, entry), out)
                                == 0) {
                            return OptionalInt.empty();
                        }
                        return OptionalInt.of(out.get(JAVA_INT, 0));
                    }
                });
    }
}
