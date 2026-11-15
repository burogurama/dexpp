package io.github.burogurama.dexpp;

import java.io.File;
import java.lang.foreign.Arena;
import java.lang.foreign.Linker;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/**
 * Finds and loads the shared C library, once per process.
 *
 * <p>The library is {@code libdexpp_c_shared.so} on Linux, {@code libdexpp_c_shared.dylib} on macOS
 * and {@code dexpp_c_shared.dll} on Windows. It is looked for in this order:
 *
 * <ol>
 *   <li>the system property {@value #PATH_PROPERTY}: the library file itself, or the folder that
 *       holds it;
 *   <li>each folder of the system property {@code java.library.path};
 *   <li>the search path of the operating system ({@code LD_LIBRARY_PATH}, {@code DYLD_LIBRARY_PATH}
 *       or {@code PATH}).
 * </ol>
 */
final class NativeLibrary {
    /** The system property that names the library file or its folder. */
    static final String PATH_PROPERTY = "dexpp.library.path";

    /** The library name without prefix and suffix, as for System.loadLibrary. */
    static final String NAME = "dexpp_c_shared";

    /** The symbols of the loaded library. */
    static final SymbolLookup LOOKUP = load();

    private NativeLibrary() {}

    private static SymbolLookup load() {
        if (ValueLayout.ADDRESS.byteSize() != 8
                || Linker.nativeLinker().canonicalLayouts().get("size_t").byteSize() != 8) {
            throw new UnsupportedOperationException("dexpp needs a 64-bit Java runtime");
        }
        String fileName = System.mapLibraryName(NAME);
        String configured = System.getProperty(PATH_PROPERTY);
        if (configured != null && !configured.isEmpty()) {
            Path path = Path.of(configured);
            if (Files.isDirectory(path)) {
                path = path.resolve(fileName);
            }
            if (!Files.isRegularFile(path)) {
                throw new UnsatisfiedLinkError(
                        "dexpp: the system property "
                                + PATH_PROPERTY
                                + " is "
                                + configured
                                + ", but there is no library at "
                                + path);
            }
            return SymbolLookup.libraryLookup(path.toAbsolutePath(), Arena.global());
        }
        List<String> searched = new ArrayList<>();
        String libraryPath = System.getProperty("java.library.path", "");
        for (String folder : libraryPath.split(File.pathSeparator)) {
            if (folder.isEmpty()) {
                continue;
            }
            Path path = Path.of(folder, fileName);
            searched.add(path.toString());
            if (Files.isRegularFile(path)) {
                return SymbolLookup.libraryLookup(path.toAbsolutePath(), Arena.global());
            }
        }
        try {
            return SymbolLookup.libraryLookup(fileName, Arena.global());
        } catch (IllegalArgumentException e) {
            UnsatisfiedLinkError error =
                    new UnsatisfiedLinkError(
                            "dexpp: cannot find "
                                    + fileName
                                    + ". Build it with the CMake target dexpp_c_shared, then set"
                                    + " the system property "
                                    + PATH_PROPERTY
                                    + " to the file or its folder, for example"
                                    + " -D"
                                    + PATH_PROPERTY
                                    + "=build. Searched java.library.path ("
                                    + String.join(", ", searched)
                                    + ") and the system search path.");
            error.initCause(e);
            throw error;
        }
    }
}
