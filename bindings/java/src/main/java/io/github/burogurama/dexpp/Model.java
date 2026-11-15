package io.github.burogurama.dexpp;

import static java.lang.foreign.ValueLayout.ADDRESS;
import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.io.IOException;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Optional;

/**
 * An editable DEX file in memory. Read a DEX file into a model, change it, and write a DEX file
 * again. From Java the model works on whole classes: inspect, copy, move, remove and rename them.
 * Changing the code inside a method is only possible from C++.
 *
 * <p>A model is separate from the analysis API: it is not an {@link AnalysisContext}. Reading a
 * model from several threads at once is safe, but a change to it must not run at the same time as
 * any other use of it.
 *
 * <p>It is a handle: {@link #close()} frees it, and a {@link java.lang.ref.Cleaner} frees it when
 * it is no longer used.
 */
public final class Model implements AutoCloseable {
    private final NativeHandle handle;

    private Model(MemorySegment pointer) {
        handle = new NativeHandle("Model", pointer, Native::dexpp_model_free);
    }

    /**
     * Makes an empty model.
     *
     * @param version the DEX format version: 35, 37, 38, 39 or 40
     */
    public Model(int version) {
        this(Native.dexpp_model_new(version));
    }

    /**
     * Reads the bytes of a DEX file into a model, with debug information.
     *
     * @param data the content of a DEX file
     * @return the model
     * @throws InvalidDexException if the data is not a valid DEX file
     * @throws UnsupportedDexException if the data uses a format the model does not read, such as
     *     the version 41 container format
     */
    public static Model read(byte[] data) {
        return read(data, true);
    }

    /**
     * Reads the bytes of a DEX file into a model.
     *
     * @param data the content of a DEX file
     * @param debugInfo true keeps line numbers and local variable names; false leaves them out,
     *     which makes the model smaller
     * @return the model
     * @throws InvalidDexException if the data is not a valid DEX file
     * @throws UnsupportedDexException if the data uses a format the model does not read, such as
     *     the version 41 container format
     */
    public static Model read(byte[] data, boolean debugInfo) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment bytes = arena.allocate(Math.max(1, data.length));
            MemorySegment.copy(data, 0, bytes, JAVA_BYTE, 0, data.length);
            MemorySegment err = Errors.slot(arena);
            MemorySegment p = Native.dexpp_model_read(bytes, data.length, debugInfo ? 1 : 0, err);
            if (Mem.isNull(p)) {
                throw Errors.unchecked(err);
            }
            return new Model(p);
        }
    }

    /**
     * Reads a DEX file into a model, with debug information.
     *
     * @param path the DEX file
     * @return the model
     * @throws java.nio.file.NoSuchFileException if the file does not exist
     * @throws IOException if the file exists but cannot be read
     * @throws InvalidDexException if the file is not a valid DEX file
     * @throws UnsupportedDexException if the file uses a format the model does not read
     */
    public static Model readFile(Path path) throws IOException {
        return readFile(path, true);
    }

    /**
     * Reads a DEX file into a model.
     *
     * @param path the DEX file
     * @param debugInfo true keeps line numbers and local variable names
     * @return the model
     * @throws java.nio.file.NoSuchFileException if the file does not exist
     * @throws IOException if the file exists but cannot be read
     * @throws InvalidDexException if the file is not a valid DEX file
     * @throws UnsupportedDexException if the file uses a format the model does not read
     */
    public static Model readFile(Path path, boolean debugInfo) throws IOException {
        String file = path.toString();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment err = Errors.slot(arena);
            MemorySegment p =
                    Native.dexpp_model_read_file(Mem.cString(arena, file), debugInfo ? 1 : 0, err);
            if (Mem.isNull(p)) {
                throw Errors.io(err, List.of(file));
            }
            return new Model(p);
        }
    }

    /** Frees the model at once. A second call does nothing. */
    @Override
    public void close() {
        handle.close();
    }

    /**
     * A full copy of the model. Changing one does not change the other.
     *
     * @return the copy
     */
    public Model copy() {
        return handle.call(m -> new Model(Native.dexpp_model_clone(m)));
    }

    /**
     * Writes the model as the bytes of a DEX file, with debug information.
     *
     * @return the bytes
     * @throws InvalidModelException if the model breaks rules of the DEX format; its {@link
     *     InvalidModelException#problems()} lists them
     * @throws PoolOverflowException if a table of the file would be too large
     * @throws VersionTooLowException if the model uses a feature its DEX version lacks
     */
    public byte[] write() {
        return write(true);
    }

    /**
     * Writes the model as the bytes of a DEX file. The model is checked first, and the same model
     * always gives the same bytes.
     *
     * @param debugInfo true writes line numbers and local variable names; false strips them
     * @return the bytes
     * @throws InvalidModelException if the model breaks rules of the DEX format; its {@link
     *     InvalidModelException#problems()} lists them
     * @throws PoolOverflowException if a table of the file would be too large
     * @throws VersionTooLowException if the model uses a feature its DEX version lacks
     */
    public byte[] write(boolean debugInfo) {
        return handle.call(m -> {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment length = arena.allocate(JAVA_LONG);
                MemorySegment err = Errors.slot(arena);
                MemorySegment buffer = Native.dexpp_model_write(m, debugInfo ? 1 : 0, length, err);
                if (Mem.isNull(buffer)) {
                    throw Errors.unchecked(err);
                }
                try {
                    return buffer.reinterpret(length.get(JAVA_LONG, 0)).toArray(JAVA_BYTE);
                } finally {
                    Native.dexpp_buffer_free(buffer);
                }
            }
        });
    }

    /**
     * Writes the model to a DEX file, with debug information.
     *
     * @param path the file to write
     * @throws IOException if the file cannot be created or written
     * @throws InvalidModelException if the model breaks rules of the DEX format
     * @throws PoolOverflowException if a table of the file would be too large
     * @throws VersionTooLowException if the model uses a feature its DEX version lacks
     */
    public void writeFile(Path path) throws IOException {
        writeFile(path, true);
    }

    /**
     * Writes the model to a DEX file.
     *
     * @param path the file to write
     * @param debugInfo true writes line numbers and local variable names
     * @throws IOException if the file cannot be created or written
     * @throws InvalidModelException if the model breaks rules of the DEX format
     * @throws PoolOverflowException if a table of the file would be too large
     * @throws VersionTooLowException if the model uses a feature its DEX version lacks
     */
    public void writeFile(Path path, boolean debugInfo) throws IOException {
        String file = path.toString();
        IOException failure =
                handle.call(m -> {
                    try (Arena arena = Arena.ofConfined()) {
                        MemorySegment err = Errors.slot(arena);
                        if (Native.dexpp_model_write_file(
                                        m, Mem.cString(arena, file), debugInfo ? 1 : 0, err)
                                != 0) {
                            return null;
                        }
                        return Errors.io(err, List.of(file));
                    }
                });
        if (failure != null) {
            throw failure;
        }
    }

    /**
     * The DEX format version that {@link #write()} uses.
     *
     * @return the version, such as 35
     */
    public int version() {
        return handle.callInt(Native::dexpp_model_version);
    }

    /**
     * Sets the DEX format version that {@link #write()} uses. Writing never raises the version by
     * itself: set it when {@link #requiredVersion()} is higher.
     *
     * @param version the version: 35, 37, 38, 39 or 40
     */
    public void setVersion(int version) {
        handle.run(m -> Native.dexpp_model_set_version(m, version));
    }

    /**
     * The lowest DEX version that has every feature the model uses. It walks the whole model.
     *
     * @return the version
     */
    public int requiredVersion() {
        return handle.callInt(Native::dexpp_model_required_version);
    }

    /**
     * How many entries each table of the written file would have. It walks the whole model.
     *
     * @return the table sizes
     */
    public PoolStats poolStats() {
        return handle.call(m -> {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment s = Native.dexpp_model_pool_stats(arena, m);
                return new PoolStats(
                        Mem.count(s, Layouts.PoolStats.STRINGS),
                        Mem.count(s, Layouts.PoolStats.TYPES),
                        Mem.count(s, Layouts.PoolStats.PROTOS),
                        Mem.count(s, Layouts.PoolStats.FIELDS),
                        Mem.count(s, Layouts.PoolStats.METHODS),
                        Mem.count(s, Layouts.PoolStats.METHOD_HANDLES),
                        Mem.count(s, Layouts.PoolStats.CALL_SITES));
            }
        });
    }

    /**
     * Every rule of the DEX format the model breaks. An empty list means {@link #write()} can
     * encode the model. Otherwise {@link #write()} throws an {@link InvalidModelException} whose
     * problems are the same list.
     *
     * @return the problems
     */
    public List<Problem> validate() {
        return handle.call(m ->
                Mem.take(
                        (arena, count) -> Native.dexpp_model_validate(m, count),
                        Layouts.Problem.LAYOUT,
                        (p, b) ->
                                new Problem(
                                        Mem.string(p, b + Layouts.Problem.WHERE),
                                        Mem.string(p, b + Layouts.Problem.MESSAGE)),
                        Native::dexpp_problem_array_free));
    }

    /**
     * The number of classes in the model. It copies nothing, unlike {@link #classDescriptors()}.
     *
     * @return the class count
     */
    public int classCount() {
        return Math.toIntExact(handle.callLong(Native::dexpp_model_class_count));
    }

    /**
     * The descriptor of every class, in model order.
     *
     * @return the descriptors
     */
    public List<String> classDescriptors() {
        return handle.call(m ->
                Mem.takeStrings((arena, count) -> Native.dexpp_model_class_descriptors(m, count)));
    }

    /**
     * Tells whether the model has a class with this descriptor. It copies nothing, unlike {@link
     * #classInfo(String)}.
     *
     * @param descriptor a class descriptor
     * @return true when the model has the class
     */
    public boolean hasClass(String descriptor) {
        return handle.callBoolean(m -> {
            try (Arena arena = Arena.ofConfined()) {
                return Native.dexpp_model_has_class(m, Mem.cString(arena, descriptor)) != 0;
            }
        });
    }

    /**
     * A summary of the class with this descriptor.
     *
     * @param descriptor a class descriptor
     * @return the summary, or empty when the model has no such class
     */
    public Optional<ClassInfo> classInfo(String descriptor) {
        return handle.call(m -> {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment info = arena.allocate(Layouts.ClassInfo.LAYOUT);
                if (Native.dexpp_model_class_info(m, Mem.cString(arena, descriptor), info) == 0) {
                    return Optional.empty();
                }
                try {
                    return Optional.of(
                            new ClassInfo(
                                    Mem.string(info, Layouts.ClassInfo.DESCRIPTOR),
                                    new AccessFlags(Mem.i32(info, Layouts.ClassInfo.ACCESS_FLAGS)),
                                    Mem.optString(info, Layouts.ClassInfo.SUPERCLASS),
                                    Mem.optString(info, Layouts.ClassInfo.SOURCE_FILE),
                                    Mem.strings(
                                            Mem.pointer(info, Layouts.ClassInfo.INTERFACES),
                                            Mem.i64(info, Layouts.ClassInfo.INTERFACE_COUNT)),
                                    Mem.count(info, Layouts.ClassInfo.FIELD_COUNT),
                                    Mem.count(info, Layouts.ClassInfo.METHOD_COUNT)));
                } finally {
                    Native.dexpp_class_info_clear(info);
                }
            }
        });
    }

    /**
     * Takes the class out of {@code source} and puts it into this model. The class is not copied,
     * so this is cheap for a large class.
     *
     * @param source the model that has the class
     * @param descriptor the class descriptor
     * @throws ModelException if {@code source} has no such class, or this model already has one
     */
    public void moveClass(Model source, String descriptor) {
        handle.run(dst ->
                source.handle.run(src -> {
                    try (Arena arena = Arena.ofConfined()) {
                        MemorySegment err = Errors.slot(arena);
                        if (Native.dexpp_model_move_class(
                                        dst, src, Mem.cString(arena, descriptor), err)
                                == 0) {
                            throw Errors.unchecked(err);
                        }
                    }
                }));
    }

    /**
     * Copies the class from {@code source} into this model. {@code source} is not changed.
     *
     * @param source the model that has the class
     * @param descriptor the class descriptor
     * @throws ModelException if {@code source} has no such class, or this model already has one
     */
    public void copyClass(Model source, String descriptor) {
        handle.run(dst ->
                source.handle.run(src -> {
                    try (Arena arena = Arena.ofConfined()) {
                        MemorySegment err = Errors.slot(arena);
                        if (Native.dexpp_model_copy_class(
                                        dst, src, Mem.cString(arena, descriptor), err)
                                == 0) {
                            throw Errors.unchecked(err);
                        }
                    }
                }));
    }

    /**
     * Removes the class with this descriptor.
     *
     * @param descriptor a class descriptor
     * @return true when a class was removed, false when there was none
     */
    public boolean removeClass(String descriptor) {
        return handle.callBoolean(m -> {
            try (Arena arena = Arena.ofConfined()) {
                return Native.dexpp_model_remove_class(m, Mem.cString(arena, descriptor)) != 0;
            }
        });
    }

    /**
     * Renames classes everywhere they are referenced. Every reference to a class in the mapping is
     * changed: the class itself, superclasses and interfaces, method and field identities, array
     * types, instructions, catch types, annotations, and the class names inside generic signatures.
     * A class that is not in the mapping is left as it is. String constants are never changed, so a
     * name passed to {@code Class.forName} keeps its old value.
     *
     * @param mapping old descriptor to new descriptor
     */
    public void renameTypes(Map<String, String> mapping) {
        List<String> from = new ArrayList<>(mapping.keySet());
        List<String> to = new ArrayList<>();
        for (String key : from) {
            to.add(mapping.get(key));
        }
        handle.run(m -> {
            if (from.isEmpty()) {
                return;
            }
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment cFrom = arena.allocate(ADDRESS, from.size());
                MemorySegment cTo = arena.allocate(ADDRESS, to.size());
                for (int i = 0; i < from.size(); i++) {
                    cFrom.setAtIndex(ADDRESS, i, Mem.cString(arena, from.get(i)));
                    cTo.setAtIndex(ADDRESS, i, Mem.cString(arena, to.get(i)));
                }
                Native.dexpp_model_rename_types(m, cFrom, cTo, from.size());
            }
        });
    }
}
