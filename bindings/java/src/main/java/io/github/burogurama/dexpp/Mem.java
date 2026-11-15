package io.github.burogurama.dexpp;

import static java.lang.foreign.ValueLayout.ADDRESS;
import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;
import static java.lang.foreign.ValueLayout.JAVA_SHORT;

import java.lang.foreign.Arena;
import java.lang.foreign.MemoryLayout;
import java.lang.foreign.MemorySegment;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.function.Function;

/**
 * Reads C memory into Java values, and frees the C side as the C API asks.
 *
 * <p>Every method copies: no Java value keeps a pointer into C memory.
 */
final class Mem {
    private Mem() {}

    /** Reads one element of a C array; {@code base} is its byte offset. */
    @FunctionalInterface
    interface Reader<T> {
        T read(MemorySegment memory, long base);
    }

    /**
     * Calls a C function that returns an array and writes its length to a {@code size_t *count}.
     * The arena lives for the call, for the arguments.
     */
    @FunctionalInterface
    interface ArrayCall {
        MemorySegment call(Arena arena, MemorySegment count);
    }

    /** A C {@code _array_free} function. */
    @FunctionalInterface
    interface ArrayFree {
        void free(MemorySegment array, long count);
    }

    // ---- single values ------------------------------------------------------

    static boolean isNull(MemorySegment pointer) {
        return pointer.address() == 0;
    }

    static MemorySegment pointer(MemorySegment memory, long offset) {
        return memory.get(ADDRESS, offset);
    }

    static int i32(MemorySegment memory, long offset) {
        return memory.get(JAVA_INT, offset);
    }

    static long i64(MemorySegment memory, long offset) {
        return memory.get(JAVA_LONG, offset);
    }

    /** A {@code size_t} count. The C API never returns more than fits in an int. */
    static int count(MemorySegment memory, long offset) {
        return Math.toIntExact(memory.get(JAVA_LONG, offset));
    }

    static int u8(MemorySegment memory, long offset) {
        return Byte.toUnsignedInt(memory.get(JAVA_BYTE, offset));
    }

    static int u16(MemorySegment memory, long offset) {
        return Short.toUnsignedInt(memory.get(JAVA_SHORT, offset));
    }

    /** A C {@code int} used as a boolean: nonzero is true. */
    static boolean bool(MemorySegment memory, long offset) {
        return memory.get(JAVA_INT, offset) != 0;
    }

    /** A C {@code int} used as 1, 0, or -1 for absent. */
    static Optional<Boolean> triState(MemorySegment memory, long offset) {
        int value = memory.get(JAVA_INT, offset);
        return value < 0 ? Optional.empty() : Optional.of(value != 0);
    }

    // ---- strings --------------------------------------------------------------

    /** Copies a NUL-terminated UTF-8 string; NULL gives null. */
    static String cString(MemorySegment pointer) {
        if (isNull(pointer)) {
            return null;
        }
        return pointer.reinterpret(Long.MAX_VALUE).getString(0, StandardCharsets.UTF_8);
    }

    /** Copies the {@code char *} member at {@code offset}; NULL gives "". */
    static String string(MemorySegment memory, long offset) {
        String s = cString(pointer(memory, offset));
        return s == null ? "" : s;
    }

    /** Copies the {@code char *} member at {@code offset}; NULL gives empty. */
    static Optional<String> optString(MemorySegment memory, long offset) {
        return Optional.ofNullable(cString(pointer(memory, offset)));
    }

    /** Copies the {@code DexppText} member at {@code offset}, NUL bytes included. */
    static String text(MemorySegment memory, long offset) {
        MemorySegment data = pointer(memory, offset + Layouts.Text.DATA);
        long length = i64(memory, offset + Layouts.Text.LENGTH);
        if (isNull(data) || length == 0) {
            return "";
        }
        return new String(data.reinterpret(length).toArray(JAVA_BYTE), StandardCharsets.UTF_8);
    }

    /** Copies a string the C side allocated, then frees it; NULL gives null. */
    static String takeCString(MemorySegment pointer) {
        try {
            return cString(pointer);
        } finally {
            Native.dexpp_string_free(pointer);
        }
    }

    /** As {@link #takeCString}, for a string that is never NULL. */
    static String takeString(MemorySegment pointer) {
        String s = takeCString(pointer);
        return s == null ? "" : s;
    }

    /** As {@link #takeCString}, for a string that may be absent. */
    static Optional<String> takeOptString(MemorySegment pointer) {
        return Optional.ofNullable(takeCString(pointer));
    }

    /** UTF-8 bytes of a Java string, in {@code arena}, with a final NUL. */
    static MemorySegment cString(Arena arena, String s) {
        return arena.allocateFrom(s, StandardCharsets.UTF_8);
    }

    // ---- identities -------------------------------------------------------------

    static MethodId methodId(MemorySegment memory, long offset) {
        return new MethodId(
                string(memory, offset + Layouts.MethodId.CLASS_DESCRIPTOR),
                string(memory, offset + Layouts.MethodId.NAME),
                string(memory, offset + Layouts.MethodId.PROTO));
    }

    static FieldId fieldId(MemorySegment memory, long offset) {
        return new FieldId(
                string(memory, offset + Layouts.FieldId.CLASS_DESCRIPTOR),
                string(memory, offset + Layouts.FieldId.NAME),
                string(memory, offset + Layouts.FieldId.TYPE));
    }

    // ---- arrays -----------------------------------------------------------------

    /** Copies {@code count} structs of an array that some struct owns. */
    static <T> List<T> array(
            MemorySegment array, long count, MemoryLayout layout, Reader<T> reader) {
        if (isNull(array) || count == 0) {
            return List.of();
        }
        long size = layout.byteSize();
        MemorySegment all = array.reinterpret(count * size);
        List<T> out = new ArrayList<>(Math.toIntExact(count));
        for (long i = 0; i < count; i++) {
            out.add(reader.read(all, i * size));
        }
        return List.copyOf(out);
    }

    /** Copies an array of {@code char *} that some struct owns. */
    static List<String> strings(MemorySegment array, long count) {
        return array(
                array,
                count,
                ADDRESS,
                (m, base) -> {
                    String s = cString(pointer(m, base));
                    return s == null ? "" : s;
                });
    }

    /** Copies an array of {@code uint32_t} that some struct owns. */
    static List<Integer> indices(MemorySegment array, long count) {
        if (isNull(array) || count == 0) {
            return List.of();
        }
        int[] values = array.reinterpret(count * Integer.BYTES).toArray(JAVA_INT);
        Integer[] boxed = new Integer[values.length];
        for (int i = 0; i < values.length; i++) {
            boxed[i] = values[i];
        }
        return List.of(boxed);
    }

    /** Copies an array of {@code uint16_t} that some struct owns. */
    static List<Integer> registers(MemorySegment array, long count) {
        if (isNull(array) || count == 0) {
            return List.of();
        }
        short[] values = array.reinterpret(count * Short.BYTES).toArray(JAVA_SHORT);
        Integer[] boxed = new Integer[values.length];
        for (int i = 0; i < values.length; i++) {
            boxed[i] = Short.toUnsignedInt(values[i]);
        }
        return List.of(boxed);
    }

    /**
     * Calls a C function that returns an array, copies the array, and frees it with {@code free},
     * also when the copy fails.
     */
    static <T> List<T> take(ArrayCall call, MemoryLayout layout, Reader<T> reader, ArrayFree free) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment countOut = arena.allocate(JAVA_LONG);
            MemorySegment array = call.call(arena, countOut);
            long count = countOut.get(JAVA_LONG, 0);
            try {
                return array(array, count, layout, reader);
            } finally {
                free.free(array, count);
            }
        }
    }

    /** {@link #take} for an array of strings. */
    static List<String> takeStrings(ArrayCall call) {
        return take(
                call,
                ADDRESS,
                (m, base) -> {
                    String s = cString(pointer(m, base));
                    return s == null ? "" : s;
                },
                Native::dexpp_string_array_free);
    }

    /** {@link #take} for an array of DexppText. */
    static List<String> takeTexts(ArrayCall call) {
        return take(call, Layouts.Text.LAYOUT, Mem::text, Native::dexpp_text_array_free);
    }

    /** {@link #take} for an array of indices. */
    static List<Integer> takeIndices(ArrayCall call) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment countOut = arena.allocate(JAVA_LONG);
            MemorySegment array = call.call(arena, countOut);
            long count = countOut.get(JAVA_LONG, 0);
            try {
                return indices(array, count);
            } finally {
                Native.dexpp_index_array_free(array, count);
            }
        }
    }

    /**
     * {@link #take} for an array of handles. Each handle becomes a Java object that owns it, then
     * the array alone is freed: an {@code _array_free} function with a count of 0 frees the block
     * and leaves the handles (dexpp.h).
     */
    static <H> List<H> takeHandles(
            ArrayCall call, Function<MemorySegment, H> wrap, ArrayFree free) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment countOut = arena.allocate(JAVA_LONG);
            MemorySegment array = call.call(arena, countOut);
            long count = countOut.get(JAVA_LONG, 0);
            try {
                return array(array, count, ADDRESS, (m, base) -> wrap.apply(pointer(m, base)));
            } finally {
                free.free(array, 0);
            }
        }
    }
}
