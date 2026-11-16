package io.github.burogurama.dexpp;

import static java.lang.foreign.ValueLayout.ADDRESS;
import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.io.IOException;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.nio.file.Path;
import java.util.List;
import java.util.Optional;
import java.util.OptionalInt;
import java.util.OptionalLong;

/**
 * An Android package: the archive, its manifest, its resources and its signatures. The code is a
 * separate object, which {@link #analysis()} returns. The manifest, the resources and the signing
 * information are each decoded on first use and kept.
 *
 * <p>It is a handle: {@link #close()} frees it, and a {@link java.lang.ref.Cleaner} frees it when
 * it is no longer used.
 */
public final class Apk implements AutoCloseable {
    private final NativeHandle handle;

    private Apk(MemorySegment pointer) {
        handle = new NativeHandle("Apk", pointer, Native::dexpp_apk_free);
    }

    NativeHandle handle() {
        return handle;
    }

    /**
     * Opens an APK file.
     *
     * @param path the APK file
     * @return the APK
     * @throws java.nio.file.NoSuchFileException if the file does not exist
     * @throws IOException if the file exists but cannot be read
     * @throws InvalidApkException if the file is not a ZIP archive
     */
    public static Apk open(Path path) throws IOException {
        String file = path.toString();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment err = Errors.slot(arena);
            MemorySegment p = Native.dexpp_apk_open(Mem.cString(arena, file), err);
            if (Mem.isNull(p)) {
                throw Errors.io(err, List.of(file));
            }
            return new Apk(p);
        }
    }

    /**
     * Opens an APK from its bytes in memory, for example bytes downloaded from the network. The
     * bytes get the same checks as a file given to {@link #open(Path)}, and the APK then works the
     * same. The APK keeps its own copy, so the array may be changed as soon as this returns. Error
     * messages start with {@code "<apk>"}.
     *
     * @param data the content of an APK file
     * @return the APK
     * @throws InvalidApkException if the bytes are not a ZIP archive
     */
    public static Apk fromBytes(byte[] data) {
        return fromBytes(data, null);
    }

    /**
     * Opens an APK from its bytes in memory, with a name for error messages.
     *
     * @param data the content of an APK file
     * @param label a name for the bytes, used at the start of error messages where {@link
     *     #open(Path)} puts the path, for example the name of the file they came from; {@link
     *     #analysis()} uses it too. null gives {@code "<apk>"}.
     * @return the APK
     * @throws InvalidApkException if the bytes are not a ZIP archive
     */
    public static Apk fromBytes(byte[] data, String label) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment bytes = arena.allocate(Math.max(1, data.length));
            MemorySegment.copy(data, 0, bytes, JAVA_BYTE, 0, data.length);
            MemorySegment name = label == null ? MemorySegment.NULL : Mem.cString(arena, label);
            MemorySegment err = Errors.slot(arena);
            MemorySegment p = Native.dexpp_apk_open_buffer(bytes, data.length, name, err);
            if (Mem.isNull(p)) {
                throw Errors.unchecked(err);
            }
            return new Apk(p);
        }
    }

    /**
     * Frees the APK handle at once. An {@link AnalysisContext} that came from {@link #analysis()}
     * stays valid. A second call does nothing.
     */
    @Override
    public void close() {
        handle.close();
    }

    /**
     * The name of every entry in the archive.
     *
     * @return the entry names
     */
    public List<String> entries() {
        return handle.call(
                a -> Mem.takeStrings((arena, count) -> Native.dexpp_apk_entries(a, count)));
    }

    /**
     * The uncompressed bytes of one archive entry.
     *
     * @param name the entry name, such as {@code "classes.dex"}
     * @return the bytes, or empty when the archive has no entry with this name; an empty entry
     *     gives an empty array
     */
    public Optional<byte[]> read(String name) {
        return handle.call(a -> {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment out = arena.allocate(ADDRESS);
                MemorySegment length = arena.allocate(JAVA_LONG);
                if (Native.dexpp_apk_read(a, Mem.cString(arena, name), out, length) == 0) {
                    return Optional.empty();
                }
                MemorySegment buffer = out.get(ADDRESS, 0);
                try {
                    long n = length.get(JAVA_LONG, 0);
                    if (Mem.isNull(buffer) || n == 0) {
                        return Optional.of(new byte[0]);
                    }
                    return Optional.of(buffer.reinterpret(n).toArray(JAVA_BYTE));
                } finally {
                    Native.dexpp_buffer_free(buffer);
                }
            }
        });
    }

    /**
     * The app's package name, from the manifest.
     *
     * @return the package name, or empty when the APK has no manifest or it cannot be decoded
     */
    public Optional<String> packageName() {
        return handle.call(a -> Mem.takeOptString(Native.dexpp_apk_package(a)));
    }

    /**
     * The code of the APK: an {@link AnalysisContext} over every {@code classes*.dex} entry, in
     * multidex order. It reuses the archive bytes the APK already holds, and lets the library
     * choose the number of threads. The same as {@code analysis(0)}.
     *
     * @return the analysis of the code
     * @throws InvalidApkException if the APK has no {@code classes.dex}, or an entry is damaged
     * @throws InvalidDexException if a DEX entry is not a valid DEX file
     */
    public AnalysisContext analysis() {
        return analysis(0);
    }

    /**
     * The code of the APK, with a given number of threads.
     *
     * <p>The analysis is built on the first call and kept, one for each thread count: a later call
     * with the same count returns a new {@link AnalysisContext} for the same analysis, so its call
     * graph and other structures are built once. A call with another count builds a second
     * analysis, which holds its own copy of the DEX files. So use one count in a program.
     *
     * @param threads how many threads decompress the DEX entries and, later, build the call graph
     *     and the cross-references, as for {@link AnalysisContext#fromApk(Path, int)}. 1 keeps all work on the calling
     *     thread. 0 lets the library choose. A negative number counts as 0.
     * @return the analysis of the code
     * @throws InvalidApkException if the APK has no {@code classes.dex}, or an entry is damaged
     * @throws InvalidDexException if a DEX entry is not a valid DEX file
     */
    public AnalysisContext analysis(int threads) {
        return handle.call(a -> {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment err = Errors.slot(arena);
                MemorySegment p = Native.dexpp_apk_analysis(a, Math.max(0, threads), err);
                if (Mem.isNull(p)) {
                    throw Errors.unchecked(err);
                }
                return new AnalysisContext(p);
            }
        });
    }

    // ---- manifest -----------------------------------------------------------------

    /**
     * The decoded manifest.
     *
     * @return the manifest, or empty when the APK has none or it cannot be decoded
     */
    public Optional<Manifest> manifest() {
        return handle.call(a -> {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment m = arena.allocate(Layouts.Manifest.LAYOUT);
                if (Native.dexpp_apk_manifest(a, m) == 0) {
                    return Optional.empty();
                }
                try {
                    return Optional.of(manifest(m));
                } finally {
                    Native.dexpp_manifest_clear(m);
                }
            }
        });
    }

    private static Manifest manifest(MemorySegment m) {
        List<DeclaredPermission> declared =
                Mem.array(
                        Mem.pointer(m, Layouts.Manifest.DECLARED_PERMISSIONS),
                        Mem.i64(m, Layouts.Manifest.DECLARED_PERMISSION_COUNT),
                        Layouts.DeclaredPermission.LAYOUT,
                        (d, b) ->
                                new DeclaredPermission(
                                        Mem.string(d, b + Layouts.DeclaredPermission.NAME),
                                        Mem.bool(
                                                        d,
                                                        b
                                                                + Layouts.DeclaredPermission
                                                                        .HAS_PROTECTION_LEVEL)
                                                ? OptionalInt.of(
                                                        Mem.i32(
                                                                d,
                                                                b
                                                                        + Layouts.DeclaredPermission
                                                                                .PROTECTION_LEVEL))
                                                : OptionalInt.empty(),
                                        Mem.bool(
                                                d,
                                                b
                                                        + Layouts.DeclaredPermission
                                                                .PROTECTION_LEVEL_VARIES)));
        List<Component> components =
                Mem.array(
                        Mem.pointer(m, Layouts.Manifest.COMPONENTS),
                        Mem.i64(m, Layouts.Manifest.COMPONENT_COUNT),
                        Layouts.Component.LAYOUT,
                        Apk::component);
        return new Manifest(
                Mem.string(m, Layouts.Manifest.PACKAGE),
                optLong(m, Layouts.Manifest.HAS_VERSION_CODE, Layouts.Manifest.VERSION_CODE),
                optReference(m, Layouts.Manifest.VERSION_CODE_REFERENCE),
                Mem.optString(m, Layouts.Manifest.VERSION_NAME),
                optReference(m, Layouts.Manifest.VERSION_NAME_REFERENCE),
                optLong(m, Layouts.Manifest.HAS_MIN_SDK, Layouts.Manifest.MIN_SDK),
                optReference(m, Layouts.Manifest.MIN_SDK_REFERENCE),
                optLong(m, Layouts.Manifest.HAS_TARGET_SDK, Layouts.Manifest.TARGET_SDK),
                optReference(m, Layouts.Manifest.TARGET_SDK_REFERENCE),
                Mem.strings(
                        Mem.pointer(m, Layouts.Manifest.PERMISSIONS),
                        Mem.i64(m, Layouts.Manifest.PERMISSION_COUNT)),
                declared,
                components,
                Mem.optString(m, Layouts.Manifest.LAUNCHER_ACTIVITY),
                Mem.triState(m, Layouts.Manifest.DEBUGGABLE),
                optReference(m, Layouts.Manifest.DEBUGGABLE_REFERENCE),
                Mem.bool(m, Layouts.Manifest.DEBUGGABLE_VARIES),
                Mem.triState(m, Layouts.Manifest.ALLOW_BACKUP),
                optReference(m, Layouts.Manifest.ALLOW_BACKUP_REFERENCE),
                Mem.bool(m, Layouts.Manifest.ALLOW_BACKUP_VARIES));
    }

    private static OptionalLong optLong(MemorySegment m, long has, long value) {
        return Mem.bool(m, has) ? OptionalLong.of(Mem.i64(m, value)) : OptionalLong.empty();
    }

    /** A {@code uint32_t} resource id where 0 means none. */
    private static OptionalInt optReference(MemorySegment m, long offset) {
        int id = Mem.i32(m, offset);
        return id == 0 ? OptionalInt.empty() : OptionalInt.of(id);
    }

    private static Component component(MemorySegment c, long b) {
        List<IntentFilter> filters =
                Mem.array(
                        Mem.pointer(c, b + Layouts.Component.INTENT_FILTERS),
                        Mem.i64(c, b + Layouts.Component.INTENT_FILTER_COUNT),
                        Layouts.IntentFilter.LAYOUT,
                        (f, at) ->
                                new IntentFilter(
                                        Mem.strings(
                                                Mem.pointer(f, at + Layouts.IntentFilter.ACTIONS),
                                                Mem.i64(f, at + Layouts.IntentFilter.ACTION_COUNT)),
                                        Mem.strings(
                                                Mem.pointer(
                                                        f, at + Layouts.IntentFilter.CATEGORIES),
                                                Mem.i64(
                                                        f,
                                                        at + Layouts.IntentFilter.CATEGORY_COUNT)),
                                        Mem.bool(f, at + Layouts.IntentFilter.IS_LAUNCHER)));
        return new Component(
                Mem.string(c, b + Layouts.Component.NAME),
                optReference(c, b + Layouts.Component.NAME_REFERENCE),
                ComponentKind.of(Mem.i32(c, b + Layouts.Component.KIND)),
                Mem.triState(c, b + Layouts.Component.EXPORTED),
                optReference(c, b + Layouts.Component.EXPORTED_REFERENCE),
                Mem.bool(c, b + Layouts.Component.EXPORTED_VARIES),
                Mem.optString(c, b + Layouts.Component.PERMISSION),
                optReference(c, b + Layouts.Component.PERMISSION_REFERENCE),
                filters,
                Mem.bool(c, b + Layouts.Component.GUARDED_BY_SIGNATURE_PERMISSION),
                Mem.bool(c, b + Layouts.Component.IS_EXPORTED),
                Mem.bool(c, b + Layouts.Component.IS_REACHABLE));
    }

    // ---- resources ----------------------------------------------------------------

    /**
     * The decoded resource table.
     *
     * @return the table, or empty when the APK has none or it cannot be decoded
     */
    public Optional<ResourceTable> resources() {
        boolean has = handle.callBoolean(a -> Native.dexpp_apk_has_resources(a) != 0);
        return has ? Optional.of(new ResourceTable(this)) : Optional.empty();
    }

    /**
     * The value of a string resource, through the resource table. Use it for a manifest attribute
     * that refers to a resource, such as {@code android:label="@string/app_name"}.
     *
     * @param id a resource id
     * @return the string, or empty when the APK has no resource table, or the id is not a string
     *     resource
     */
    public Optional<String> resolveString(int id) {
        return handle.call(a -> {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment out = arena.allocate(Layouts.Text.LAYOUT);
                if (Native.dexpp_apk_resolve_string(a, id, out) == 0) {
                    return Optional.empty();
                }
                try {
                    return Optional.of(Mem.text(out, 0));
                } finally {
                    Native.dexpp_text_clear(out);
                }
            }
        });
    }

    // ---- signing ------------------------------------------------------------------

    /**
     * The signature schemes and certificates of the APK. An unsigned APK gives a {@link
     * SigningInfo} whose {@link SigningInfo#isSigned()} is false.
     *
     * @return the signing information
     */
    public SigningInfo signing() {
        return handle.call(a -> {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment s = arena.allocate(Layouts.SigningInfo.LAYOUT);
                Native.dexpp_apk_signing(a, s);
                try {
                    List<Certificate> certificates =
                            Mem.array(
                                    Mem.pointer(s, Layouts.SigningInfo.CERTIFICATES),
                                    Mem.i64(s, Layouts.SigningInfo.CERTIFICATE_COUNT),
                                    Layouts.Certificate.LAYOUT,
                                    Apk::certificate);
                    return new SigningInfo(
                            Mem.bool(s, Layouts.SigningInfo.V1),
                            Mem.bool(s, Layouts.SigningInfo.V2),
                            Mem.bool(s, Layouts.SigningInfo.V3),
                            certificates);
                } finally {
                    Native.dexpp_signing_info_clear(s);
                }
            }
        });
    }

    private static Certificate certificate(MemorySegment c, long b) {
        MemorySegment der = Mem.pointer(c, b + Layouts.Certificate.DER);
        long length = Mem.i64(c, b + Layouts.Certificate.DER_LENGTH);
        byte[] bytes =
                Mem.isNull(der) || length == 0
                        ? new byte[0]
                        : der.reinterpret(length).toArray(JAVA_BYTE);
        return new Certificate(
                bytes,
                Mem.string(c, b + Layouts.Certificate.SUBJECT),
                Mem.string(c, b + Layouts.Certificate.ISSUER),
                Mem.string(c, b + Layouts.Certificate.SERIAL_HEX),
                Mem.string(c, b + Layouts.Certificate.NOT_BEFORE),
                Mem.string(c, b + Layouts.Certificate.NOT_AFTER),
                Mem.string(c, b + Layouts.Certificate.SHA1_HEX),
                Mem.string(c, b + Layouts.Certificate.SHA256_HEX),
                Mem.bool(c, b + Layouts.Certificate.SELF_SIGNED));
    }
}
