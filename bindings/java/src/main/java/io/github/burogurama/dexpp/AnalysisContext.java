package io.github.burogurama.dexpp;

import static java.lang.foreign.ValueLayout.ADDRESS;

import java.io.IOException;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;

/**
 * The code of a program: one or more DEX files loaded together. It is the starting point for code
 * analysis.
 *
 * <p>Loading does little work. Class data, bytecode and the whole-program structures (the call
 * graph, the class hierarchy and the cross-reference index) are built the first time they are
 * needed, and kept.
 *
 * <p>It is a handle: {@link #close()} frees it, and a {@link java.lang.ref.Cleaner} frees it when
 * it is no longer used. Every handle that comes from it, such as a {@link DexClass}, keeps the
 * analysis alive, so it stays usable after the context is closed.
 */
public final class AnalysisContext implements AutoCloseable {
    private final NativeHandle handle;

    AnalysisContext(MemorySegment pointer) {
        handle = new NativeHandle("AnalysisContext", pointer, Native::dexpp_context_free);
    }

    /**
     * Loads one DEX file.
     *
     * @param path the DEX file
     * @return the analysis of the file
     * @throws java.nio.file.NoSuchFileException if the file does not exist
     * @throws IOException if the file exists but cannot be read, for example because it is a folder
     * @throws InvalidDexException if the file is not a valid DEX file
     */
    public static AnalysisContext fromDex(Path path) throws IOException {
        String file = path.toString();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment err = Errors.slot(arena);
            MemorySegment p = Native.dexpp_context_from_dex(Mem.cString(arena, file), err);
            if (Mem.isNull(p)) {
                throw Errors.io(err, List.of(file));
            }
            return new AnalysisContext(p);
        }
    }

    /**
     * Loads several DEX files into one context, for example the files of a multidex app that are
     * already unpacked. With no file at all, the context is empty.
     *
     * @param paths the DEX files, in multidex order
     * @return the analysis of all the files
     * @throws java.nio.file.NoSuchFileException if a file does not exist
     * @throws IOException if a file exists but cannot be read
     * @throws InvalidDexException if a file is not a valid DEX file
     */
    public static AnalysisContext fromDexFiles(List<Path> paths) throws IOException {
        List<String> files = new ArrayList<>();
        for (Path path : paths) {
            files.add(path.toString());
        }
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment array = arena.allocate(ADDRESS, Math.max(1, files.size()));
            for (int i = 0; i < files.size(); i++) {
                array.setAtIndex(ADDRESS, i, Mem.cString(arena, files.get(i)));
            }
            MemorySegment err = Errors.slot(arena);
            MemorySegment p = Native.dexpp_context_from_dex_files(array, files.size(), err);
            if (Mem.isNull(p)) {
                throw Errors.io(err, files);
            }
            return new AnalysisContext(p);
        }
    }

    /**
     * Loads every top-level {@code classes*.dex} entry of an APK, in multidex order: {@code
     * classes.dex}, {@code classes2.dex}, {@code classes3.dex} and so on. The library chooses how
     * many threads decompress the entries.
     *
     * @param path the APK file
     * @return the analysis of the APK's code
     * @throws java.nio.file.NoSuchFileException if the file does not exist
     * @throws IOException if the file exists but cannot be read
     * @throws InvalidApkException if the file is not a ZIP archive, or has no {@code classes.dex}
     * @throws InvalidDexException if a DEX entry is not a valid DEX file
     */
    public static AnalysisContext fromApk(Path path) throws IOException {
        return fromApk(path, 0);
    }

    /**
     * Loads every top-level {@code classes*.dex} entry of an APK, with a given number of threads.
     *
     * @param path the APK file
     * @param threads how many threads decompress the DEX entries and, later, build the call graph.
     *     1 keeps all work on the calling thread; use it when the program already analyses several
     *     APKs in parallel. 0 lets the library choose. A negative number counts as 0.
     * @return the analysis of the APK's code
     * @throws java.nio.file.NoSuchFileException if the file does not exist
     * @throws IOException if the file exists but cannot be read
     * @throws InvalidApkException if the file is not a ZIP archive, or has no {@code classes.dex}
     * @throws InvalidDexException if a DEX entry is not a valid DEX file
     */
    public static AnalysisContext fromApk(Path path, int threads) throws IOException {
        String file = path.toString();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment err = Errors.slot(arena);
            MemorySegment p =
                    Native.dexpp_context_from_apk(
                            Mem.cString(arena, file), Math.max(0, threads), err);
            if (Mem.isNull(p)) {
                throw Errors.io(err, List.of(file));
            }
            return new AnalysisContext(p);
        }
    }

    /**
     * Frees the context at once. Handles that came from it stay valid. A second call does nothing.
     */
    @Override
    public void close() {
        handle.close();
    }

    /**
     * Every class of every loaded DEX file, in file order.
     *
     * @return the classes
     */
    public List<DexClass> classes() {
        return handle.call(ctx ->
                Mem.takeHandles(
                        (arena, count) -> Native.dexpp_context_classes(ctx, count),
                        DexClass::new,
                        Native::dexpp_class_array_free));
    }

    /**
     * The class with this descriptor.
     *
     * @param descriptor a class descriptor, such as {@code "Lcom/example/Foo;"}
     * @return the class, or empty when no loaded file defines it
     */
    public Optional<DexClass> findClass(String descriptor) {
        return handle.call(ctx -> {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment p =
                        Native.dexpp_context_find_class(ctx, Mem.cString(arena, descriptor));
                return Mem.isNull(p) ? Optional.empty() : Optional.of(new DexClass(p));
            }
        });
    }

    /**
     * Every string of every loaded DEX file's string table. This includes the names of classes,
     * methods and fields, not only the strings the code loads, and a string that is in two DEX
     * files appears twice. It decodes the whole table, so it is one of the more expensive calls.
     * For only the strings the code loads, use {@link Xrefs#referencedStrings()}.
     *
     * @return the strings, in table order
     */
    public List<String> strings() {
        return handle.call(
                ctx -> Mem.takeTexts((arena, count) -> Native.dexpp_context_strings(ctx, count)));
    }

    /**
     * The call graph of the whole program. It is built on the first call and kept, so later calls
     * are cheap.
     *
     * @return a handle on the call graph
     * @throws DexppException with {@link ErrorCode#INTERNAL} if it cannot be built
     */
    public CallGraph callGraph() {
        return handle.call(ctx -> {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment err = Errors.slot(arena);
                MemorySegment p = Native.dexpp_context_call_graph(ctx, err);
                if (Mem.isNull(p)) {
                    throw Errors.unchecked(err);
                }
                return new CallGraph(p);
            }
        });
    }

    /**
     * The class hierarchy of the whole program. It is built on the first call and kept.
     *
     * @return a handle on the hierarchy
     * @throws DexppException with {@link ErrorCode#INTERNAL} if it cannot be built
     */
    public ClassHierarchy classHierarchy() {
        return handle.call(ctx -> {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment err = Errors.slot(arena);
                MemorySegment p = Native.dexpp_context_class_hierarchy(ctx, err);
                if (Mem.isNull(p)) {
                    throw Errors.unchecked(err);
                }
                return new ClassHierarchy(p);
            }
        });
    }

    /**
     * The cross-reference index of the whole program. It is built on the first call and kept.
     *
     * @return a handle on the index
     * @throws DexppException with {@link ErrorCode#INTERNAL} if it cannot be built
     */
    public Xrefs xrefs() {
        return handle.call(ctx -> {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment err = Errors.slot(arena);
                MemorySegment p = Native.dexpp_context_xrefs(ctx, err);
                if (Mem.isNull(p)) {
                    throw Errors.unchecked(err);
                }
                return new Xrefs(p);
            }
        });
    }
}
