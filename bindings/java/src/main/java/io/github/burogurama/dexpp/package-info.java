/**
 * The Java binding of dex++: static analysis of Android APK and DEX files, and DEX editing. It
 * calls the C API of dex++ ({@code bindings/c}) through the Foreign Function &amp; Memory API, so
 * it needs Java 22 or newer.
 *
 * <p>It reaches the same API as the Go binding:
 *
 * <ul>
 *   <li>loading one DEX file, several DEX files, or every DEX file of an APK ({@link
 *       io.github.burogurama.dexpp.AnalysisContext});
 *   <li>classes, class references, methods and fields, with access flags;
 *   <li>decoded bytecode ({@link io.github.burogurama.dexpp.Instruction}), control-flow graphs, try
 *       blocks, annotations with typed values, and the initial values of static fields;
 *   <li>the calls, field accesses, string loads and type uses of a method, and its parameter
 *       registers;
 *   <li>the call graph, the class hierarchy and the cross-reference index;
 *   <li>the APK: archive entries, the manifest, resources and signing ({@link
 *       io.github.burogurama.dexpp.Apk});
 *   <li>the editable DEX model, a whole class at a time ({@link io.github.burogurama.dexpp.Model}).
 * </ul>
 *
 * <h2>The shared library</h2>
 *
 * <p>The package loads the shared C library of dex++, which the CMake target {@code dexpp_c_shared}
 * builds. Set the system property {@code dexpp.library.path} to the library file or to its folder,
 * or put the folder on {@code java.library.path}. Run Java with {@code
 * --enable-native-access=ALL-UNNAMED} (or the name of your module), or it prints a warning when it
 * loads the library.
 *
 * <h2>Handles</h2>
 *
 * <p>{@code AnalysisContext}, {@code Apk}, {@code DexClass}, {@code ClassRef}, {@code DexMethod},
 * {@code DexField}, {@code CallGraph}, {@code ClassHierarchy}, {@code Xrefs} and {@code Model} are
 * handles: each one owns an object on the C side. They are {@link java.lang.AutoCloseable}: {@code
 * close()} frees the C object at once, and a second call does nothing. A {@link
 * java.lang.ref.Cleaner} frees it when the handle is no longer used without being closed. Calling
 * any other method on a closed handle throws an {@link java.lang.IllegalStateException} that names
 * the type.
 *
 * <p>Every handle keeps the analysis it came from alive. So a {@code DexClass} stays usable after
 * its {@code AnalysisContext} is closed. Everything else, such as strings, lists and records, is
 * copied into Java memory and needs no cleanup.
 *
 * <h2>Values that may be absent</h2>
 *
 * <p>A value that may be absent is never an empty string or null. It is an {@link
 * java.util.Optional}, {@link java.util.OptionalInt} or {@link java.util.OptionalLong}, such as
 * {@code DexClass.superclass()} and {@code Manifest.versionName()}.
 *
 * <h2>Errors</h2>
 *
 * <p>A file that cannot be opened is an {@link java.io.IOException}: a {@link
 * java.nio.file.NoSuchFileException} when it does not exist, a {@link
 * java.nio.file.FileSystemException} otherwise. Every other error is a {@link
 * io.github.burogurama.dexpp.DexppException}, which is unchecked and carries the {@link
 * io.github.burogurama.dexpp.ErrorCode} of the C API. Its subclasses mirror the Python exceptions,
 * such as {@link io.github.burogurama.dexpp.InvalidDexException} and {@link
 * io.github.burogurama.dexpp.InvalidModelException}.
 *
 * <h2>Threads</h2>
 *
 * <p>Reading from one analysis on several threads at once is safe. Do not close a handle while
 * another thread still uses it.
 */
package io.github.burogurama.dexpp;
