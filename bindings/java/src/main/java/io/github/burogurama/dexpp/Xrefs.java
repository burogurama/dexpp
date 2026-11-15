package io.github.burogurama.dexpp;

import static java.lang.foreign.ValueLayout.JAVA_BYTE;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Optional;

/**
 * The cross-reference index of the whole program: who uses a method, a field, a string or a type.
 * Get it from {@link AnalysisContext#xrefs()}.
 *
 * <p>Every query returns a list of sites. A site has a code offset and a referrer: the number of
 * the method whose code holds the instruction. {@link #referrerId(int)} and {@link
 * #referrerMethod(int)} turn that number into the method. Only instructions are indexed.
 *
 * <p>It is a handle: {@link #close()} frees it, and a {@link java.lang.ref.Cleaner} frees it when
 * it is no longer used. The index itself stays in the analysis.
 */
public final class Xrefs implements AutoCloseable {
    private final NativeHandle handle;

    Xrefs(MemorySegment pointer) {
        handle = new NativeHandle("Xrefs", pointer, Native::dexpp_xrefs_free);
    }

    /** Frees the handle at once. A second call does nothing. */
    @Override
    public void close() {
        handle.close();
    }

    /**
     * The number of referrers: the methods that hold at least one indexed instruction.
     *
     * @return the referrer count
     */
    public int referrerCount() {
        return Math.toIntExact(handle.callLong(Native::dexpp_xrefs_referrer_count));
    }

    /**
     * The identity of every referrer, in number order, so {@code referrers().get(site.referrer())}
     * is the method a site is in. It copies the whole table: call it once, not in a loop.
     *
     * @return the identities
     */
    public List<MethodId> referrers() {
        return handle.call(x ->
                Mem.take(
                        (arena, count) -> Native.dexpp_xrefs_referrers(x, count),
                        Layouts.MethodId.LAYOUT,
                        Mem::methodId,
                        Native::dexpp_method_id_array_free));
    }

    /**
     * The identity of one referrer.
     *
     * @param referrer a referrer number, from a site
     * @return the identity, or empty when the number is out of range
     */
    public Optional<MethodId> referrerId(int referrer) {
        return handle.call(x -> {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment out = arena.allocate(Layouts.MethodId.LAYOUT);
                if (Native.dexpp_xrefs_referrer_id(x, referrer, out) == 0) {
                    return Optional.empty();
                }
                try {
                    return Optional.of(Mem.methodId(out, 0));
                } finally {
                    Native.dexpp_method_id_clear(out);
                }
            }
        });
    }

    /**
     * The method of one referrer.
     *
     * @param referrer a referrer number, from a site
     * @return the method, or empty when the number is out of range
     */
    public Optional<DexMethod> referrerMethod(int referrer) {
        return handle.call(x -> {
            MemorySegment m = Native.dexpp_xrefs_referrer_method(x, referrer);
            return Mem.isNull(m) ? Optional.empty() : Optional.of(new DexMethod(m));
        });
    }

    private static MethodXref methodXref(MemorySegment m, long b) {
        return new MethodXref(
                Mem.i32(m, b + Layouts.MethodXref.REFERRER),
                Mem.i32(m, b + Layouts.MethodXref.CODE_OFFSET),
                InvokeKind.of(Mem.i32(m, b + Layouts.MethodXref.KIND)));
    }

    /**
     * The call sites whose target, as the bytecode names it, is exactly this method. Virtual
     * dispatch is not applied: a call to {@code Animal.sound} is not listed under {@code
     * Dog.sound}. The {@link CallGraph} applies it.
     *
     * @param id the called method
     * @return the call sites
     */
    public List<MethodXref> methodRefs(MethodId id) {
        return handle.call(x ->
                Mem.take(
                        (arena, count) ->
                                Native.dexpp_xrefs_method_refs(
                                        x,
                                        Mem.cString(arena, id.classDescriptor()),
                                        Mem.cString(arena, id.name()),
                                        Mem.cString(arena, id.proto()),
                                        count),
                        Layouts.MethodXref.LAYOUT,
                        Xrefs::methodXref,
                        Native::dexpp_method_xref_array_free));
    }

    /**
     * The call sites of every method with this name, in any class and with any prototype, grouped
     * by called method. The groups come in no fixed order. It is the quick way to find every call
     * to a name such as {@code "exec"} or {@code "loadUrl"}.
     *
     * @param name a method name
     * @return the groups
     */
    public List<MethodRefGroup> methodRefsByName(String name) {
        return handle.call(x ->
                Mem.take(
                        (arena, count) ->
                                Native.dexpp_xrefs_method_refs_by_name(
                                        x, Mem.cString(arena, name), count),
                        Layouts.MethodRefGroup.LAYOUT,
                        (m, b) ->
                                new MethodRefGroup(
                                        Mem.methodId(m, b + Layouts.MethodRefGroup.TARGET),
                                        Mem.array(
                                                Mem.pointer(m, b + Layouts.MethodRefGroup.SITES),
                                                Mem.i64(m, b + Layouts.MethodRefGroup.SITE_COUNT),
                                                Layouts.MethodXref.LAYOUT,
                                                Xrefs::methodXref)),
                        Native::dexpp_method_ref_group_array_free));
    }

    /** A C field query: class, name and type, then the count. */
    @FunctionalInterface
    private interface FieldQuery {
        MemorySegment run(
                MemorySegment xrefs,
                MemorySegment cls,
                MemorySegment name,
                MemorySegment type,
                MemorySegment count);
    }

    private List<FieldXref> fieldQuery(FieldId id, FieldQuery query) {
        return handle.call(x ->
                Mem.take(
                        (arena, count) ->
                                query.run(
                                        x,
                                        Mem.cString(arena, id.classDescriptor()),
                                        Mem.cString(arena, id.name()),
                                        Mem.cString(arena, id.type()),
                                        count),
                        Layouts.FieldXref.LAYOUT,
                        (m, b) ->
                                new FieldXref(
                                        Mem.i32(m, b + Layouts.FieldXref.REFERRER),
                                        Mem.i32(m, b + Layouts.FieldXref.CODE_OFFSET),
                                        Mem.bool(m, b + Layouts.FieldXref.IS_WRITE),
                                        Mem.bool(m, b + Layouts.FieldXref.IS_STATIC)),
                        Native::dexpp_field_xref_array_free));
    }

    /**
     * Every read and write of this field.
     *
     * @param id the field
     * @return the sites
     */
    public List<FieldXref> fieldRefs(FieldId id) {
        return fieldQuery(id, Native::dexpp_xrefs_field_refs);
    }

    /**
     * Only the reads of this field.
     *
     * @param id the field
     * @return the sites
     */
    public List<FieldXref> fieldReads(FieldId id) {
        return fieldQuery(id, Native::dexpp_xrefs_field_reads);
    }

    /**
     * Only the writes of this field.
     *
     * @param id the field
     * @return the sites
     */
    public List<FieldXref> fieldWrites(FieldId id) {
        return fieldQuery(id, Native::dexpp_xrefs_field_writes);
    }

    /**
     * The {@code const-string} instructions that load exactly this string. The string may contain
     * NUL characters. To search by substring or pattern, filter {@link #referencedStrings()} first.
     *
     * @param value the string
     * @return the sites
     */
    public List<StringXref> stringRefs(String value) {
        byte[] bytes = value.getBytes(StandardCharsets.UTF_8);
        return handle.call(x ->
                Mem.take(
                        (arena, count) -> {
                            MemorySegment data = arena.allocate(Math.max(1, bytes.length));
                            MemorySegment.copy(bytes, 0, data, JAVA_BYTE, 0, bytes.length);
                            return Native.dexpp_xrefs_string_refs(x, data, bytes.length, count);
                        },
                        Layouts.StringXref.LAYOUT,
                        (m, b) ->
                                new StringXref(
                                        Mem.i32(m, b + Layouts.StringXref.REFERRER),
                                        Mem.i32(m, b + Layouts.StringXref.CODE_OFFSET)),
                        Native::dexpp_string_xref_array_free));
    }

    /**
     * Every distinct string that some {@code const-string} instruction loads.
     *
     * @return the strings
     */
    public List<String> referencedStrings() {
        return handle.call(x ->
                Mem.takeTexts((arena, count) -> Native.dexpp_xrefs_referenced_strings(x, count)));
    }

    /**
     * The instructions that name this type: {@code const-class}, {@code check-cast}, {@code
     * instance-of}, {@code new-instance}, {@code new-array} and {@code filled-new-array}.
     *
     * @param descriptor a type descriptor
     * @return the sites
     */
    public List<TypeXref> typeRefs(String descriptor) {
        return handle.call(x ->
                Mem.take(
                        (arena, count) ->
                                Native.dexpp_xrefs_type_refs(
                                        x, Mem.cString(arena, descriptor), count),
                        Layouts.TypeXref.LAYOUT,
                        (m, b) ->
                                new TypeXref(
                                        Mem.i32(m, b + Layouts.TypeXref.REFERRER),
                                        Mem.i32(m, b + Layouts.TypeXref.CODE_OFFSET),
                                        TypeUseKind.of(Mem.i32(m, b + Layouts.TypeXref.KIND))),
                        Native::dexpp_type_xref_array_free));
    }

    /**
     * Tells whether nothing is indexed.
     *
     * @return true for an empty index
     */
    public boolean isEmpty() {
        return handle.callBoolean(x -> Native.dexpp_xrefs_empty(x) != 0);
    }
}
