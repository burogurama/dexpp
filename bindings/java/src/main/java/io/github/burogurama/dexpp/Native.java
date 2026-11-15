package io.github.burogurama.dexpp;

import static java.lang.foreign.ValueLayout.ADDRESS;
import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;
import static java.lang.foreign.ValueLayout.JAVA_SHORT;

import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemoryLayout;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SegmentAllocator;
import java.lang.invoke.MethodHandle;

/**
 * One Java method for each function of the C API (bindings/c/dexpp.h and bindings/c/dexpp_model.h).
 * This is the only class that calls C.
 *
 * <p>Each method has the name of its C function and takes its arguments in the same order. The Java
 * types follow the C types: every pointer is a {@link MemorySegment}, {@code size_t} and {@code
 * int64_t} are {@code long}, {@code int}, {@code uint32_t} and the enums are {@code int}, {@code
 * uint16_t} is {@code short} and {@code uint8_t} is {@code byte}. The one function that returns a
 * struct by value, {@code dexpp_model_pool_stats}, takes the allocator for the result first.
 *
 * <p>The methods do no checks and no conversions: the rest of the package owns the memory rules of
 * the C API. A test reads the two headers and checks that this class has exactly their functions,
 * with matching parameter and return types.
 */
final class Native {
    private Native() {}

    private static final Linker LINKER = Linker.nativeLinker();

    private static MethodHandle downcall(String name, MemoryLayout result, MemoryLayout... args) {
        return LINKER.downcallHandle(symbol(name), FunctionDescriptor.of(result, args));
    }

    private static MethodHandle downcallVoid(String name, MemoryLayout... args) {
        return LINKER.downcallHandle(symbol(name), FunctionDescriptor.ofVoid(args));
    }

    private static MemorySegment symbol(String name) {
        return NativeLibrary.LOOKUP
                .find(name)
                .orElseThrow(() ->
                        new UnsatisfiedLinkError(
                                "dexpp: the library has no function "
                                        + name
                                        + "; it may come from another version of dex++"));
    }

    /** A downcall throws only when it is called with the wrong types. */
    private static RuntimeException failure(Throwable t) {
        if (t instanceof RuntimeException e) {
            return e;
        }
        if (t instanceof Error e) {
            throw e;
        }
        return new IllegalStateException(t);
    }

    // ---- dexpp.h -------------------------------------------------------

    private static final MethodHandle dexpp_error_code =
            downcall("dexpp_error_code", JAVA_INT, ADDRESS);

    static int dexpp_error_code(MemorySegment error) {
        try {
            return (int) dexpp_error_code.invokeExact(error);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_error_message =
            downcall("dexpp_error_message", ADDRESS, ADDRESS);

    static MemorySegment dexpp_error_message(MemorySegment error) {
        try {
            return (MemorySegment) dexpp_error_message.invokeExact(error);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_error_problem_count =
            downcall("dexpp_error_problem_count", JAVA_LONG, ADDRESS);

    static long dexpp_error_problem_count(MemorySegment error) {
        try {
            return (long) dexpp_error_problem_count.invokeExact(error);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_error_problem_where =
            downcall("dexpp_error_problem_where", ADDRESS, ADDRESS, JAVA_LONG);

    static MemorySegment dexpp_error_problem_where(MemorySegment error, long index) {
        try {
            return (MemorySegment) dexpp_error_problem_where.invokeExact(error, index);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_error_problem_message =
            downcall("dexpp_error_problem_message", ADDRESS, ADDRESS, JAVA_LONG);

    static MemorySegment dexpp_error_problem_message(MemorySegment error, long index) {
        try {
            return (MemorySegment) dexpp_error_problem_message.invokeExact(error, index);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_error_free = downcallVoid("dexpp_error_free", ADDRESS);

    static void dexpp_error_free(MemorySegment error) {
        try {
            dexpp_error_free.invokeExact(error);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_string_free =
            downcallVoid("dexpp_string_free", ADDRESS);

    static void dexpp_string_free(MemorySegment s) {
        try {
            dexpp_string_free.invokeExact(s);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_string_array_free =
            downcallVoid("dexpp_string_array_free", ADDRESS, JAVA_LONG);

    static void dexpp_string_array_free(MemorySegment strings, long count) {
        try {
            dexpp_string_array_free.invokeExact(strings, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_text_clear = downcallVoid("dexpp_text_clear", ADDRESS);

    static void dexpp_text_clear(MemorySegment text) {
        try {
            dexpp_text_clear.invokeExact(text);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_text_array_free =
            downcallVoid("dexpp_text_array_free", ADDRESS, JAVA_LONG);

    static void dexpp_text_array_free(MemorySegment texts, long count) {
        try {
            dexpp_text_array_free.invokeExact(texts, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_buffer_free =
            downcallVoid("dexpp_buffer_free", ADDRESS);

    static void dexpp_buffer_free(MemorySegment buffer) {
        try {
            dexpp_buffer_free.invokeExact(buffer);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_index_array_free =
            downcallVoid("dexpp_index_array_free", ADDRESS, JAVA_LONG);

    static void dexpp_index_array_free(MemorySegment indices, long count) {
        try {
            dexpp_index_array_free.invokeExact(indices, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_method_id_clear =
            downcallVoid("dexpp_method_id_clear", ADDRESS);

    static void dexpp_method_id_clear(MemorySegment id) {
        try {
            dexpp_method_id_clear.invokeExact(id);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_method_id_array_free =
            downcallVoid("dexpp_method_id_array_free", ADDRESS, JAVA_LONG);

    static void dexpp_method_id_array_free(MemorySegment ids, long count) {
        try {
            dexpp_method_id_array_free.invokeExact(ids, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_value_clear =
            downcallVoid("dexpp_value_clear", ADDRESS);

    static void dexpp_value_clear(MemorySegment value) {
        try {
            dexpp_value_clear.invokeExact(value);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_annotation_array_free =
            downcallVoid("dexpp_annotation_array_free", ADDRESS, JAVA_LONG);

    static void dexpp_annotation_array_free(MemorySegment annotations, long count) {
        try {
            dexpp_annotation_array_free.invokeExact(annotations, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_context_from_dex =
            downcall("dexpp_context_from_dex", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_context_from_dex(MemorySegment path, MemorySegment err) {
        try {
            return (MemorySegment) dexpp_context_from_dex.invokeExact(path, err);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_context_from_dex_files =
            downcall("dexpp_context_from_dex_files", ADDRESS, ADDRESS, JAVA_LONG, ADDRESS);

    static MemorySegment dexpp_context_from_dex_files(
            MemorySegment paths, long count, MemorySegment err) {
        try {
            return (MemorySegment) dexpp_context_from_dex_files.invokeExact(paths, count, err);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_context_from_apk =
            downcall("dexpp_context_from_apk", ADDRESS, ADDRESS, JAVA_INT, ADDRESS);

    static MemorySegment dexpp_context_from_apk(
            MemorySegment path, int threads, MemorySegment err) {
        try {
            return (MemorySegment) dexpp_context_from_apk.invokeExact(path, threads, err);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_context_free =
            downcallVoid("dexpp_context_free", ADDRESS);

    static void dexpp_context_free(MemorySegment ctx) {
        try {
            dexpp_context_free.invokeExact(ctx);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_context_classes =
            downcall("dexpp_context_classes", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_context_classes(MemorySegment ctx, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_context_classes.invokeExact(ctx, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_context_find_class =
            downcall("dexpp_context_find_class", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_context_find_class(MemorySegment ctx, MemorySegment descriptor) {
        try {
            return (MemorySegment) dexpp_context_find_class.invokeExact(ctx, descriptor);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_context_strings =
            downcall("dexpp_context_strings", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_context_strings(MemorySegment ctx, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_context_strings.invokeExact(ctx, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_context_call_graph =
            downcall("dexpp_context_call_graph", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_context_call_graph(MemorySegment ctx, MemorySegment err) {
        try {
            return (MemorySegment) dexpp_context_call_graph.invokeExact(ctx, err);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_context_class_hierarchy =
            downcall("dexpp_context_class_hierarchy", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_context_class_hierarchy(MemorySegment ctx, MemorySegment err) {
        try {
            return (MemorySegment) dexpp_context_class_hierarchy.invokeExact(ctx, err);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_context_xrefs =
            downcall("dexpp_context_xrefs", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_context_xrefs(MemorySegment ctx, MemorySegment err) {
        try {
            return (MemorySegment) dexpp_context_xrefs.invokeExact(ctx, err);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_name =
            downcall("dexpp_class_name", ADDRESS, ADDRESS);

    static MemorySegment dexpp_class_name(MemorySegment cls) {
        try {
            return (MemorySegment) dexpp_class_name.invokeExact(cls);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_package =
            downcall("dexpp_class_package", ADDRESS, ADDRESS);

    static MemorySegment dexpp_class_package(MemorySegment cls) {
        try {
            return (MemorySegment) dexpp_class_package.invokeExact(cls);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_superclass =
            downcall("dexpp_class_superclass", ADDRESS, ADDRESS);

    static MemorySegment dexpp_class_superclass(MemorySegment cls) {
        try {
            return (MemorySegment) dexpp_class_superclass.invokeExact(cls);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_interfaces =
            downcall("dexpp_class_interfaces", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_class_interfaces(MemorySegment cls, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_class_interfaces.invokeExact(cls, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_access_flags =
            downcall("dexpp_class_access_flags", JAVA_INT, ADDRESS);

    static int dexpp_class_access_flags(MemorySegment cls) {
        try {
            return (int) dexpp_class_access_flags.invokeExact(cls);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_is_interface =
            downcall("dexpp_class_is_interface", JAVA_INT, ADDRESS);

    static int dexpp_class_is_interface(MemorySegment cls) {
        try {
            return (int) dexpp_class_is_interface.invokeExact(cls);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_is_abstract =
            downcall("dexpp_class_is_abstract", JAVA_INT, ADDRESS);

    static int dexpp_class_is_abstract(MemorySegment cls) {
        try {
            return (int) dexpp_class_is_abstract.invokeExact(cls);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_source_file =
            downcall("dexpp_class_source_file", ADDRESS, ADDRESS);

    static MemorySegment dexpp_class_source_file(MemorySegment cls) {
        try {
            return (MemorySegment) dexpp_class_source_file.invokeExact(cls);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_methods =
            downcall("dexpp_class_methods", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_class_methods(MemorySegment cls, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_class_methods.invokeExact(cls, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_fields =
            downcall("dexpp_class_fields", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_class_fields(MemorySegment cls, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_class_fields.invokeExact(cls, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_annotations =
            downcall("dexpp_class_annotations", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_class_annotations(MemorySegment cls, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_class_annotations.invokeExact(cls, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_free = downcallVoid("dexpp_class_free", ADDRESS);

    static void dexpp_class_free(MemorySegment cls) {
        try {
            dexpp_class_free.invokeExact(cls);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_array_free =
            downcallVoid("dexpp_class_array_free", ADDRESS, JAVA_LONG);

    static void dexpp_class_array_free(MemorySegment classes, long count) {
        try {
            dexpp_class_array_free.invokeExact(classes, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_ref_descriptor =
            downcall("dexpp_class_ref_descriptor", ADDRESS, ADDRESS);

    static MemorySegment dexpp_class_ref_descriptor(MemorySegment ref) {
        try {
            return (MemorySegment) dexpp_class_ref_descriptor.invokeExact(ref);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_ref_is_resolved =
            downcall("dexpp_class_ref_is_resolved", JAVA_INT, ADDRESS);

    static int dexpp_class_ref_is_resolved(MemorySegment ref) {
        try {
            return (int) dexpp_class_ref_is_resolved.invokeExact(ref);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_ref_resolve =
            downcall("dexpp_class_ref_resolve", ADDRESS, ADDRESS);

    static MemorySegment dexpp_class_ref_resolve(MemorySegment ref) {
        try {
            return (MemorySegment) dexpp_class_ref_resolve.invokeExact(ref);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_ref_free =
            downcallVoid("dexpp_class_ref_free", ADDRESS);

    static void dexpp_class_ref_free(MemorySegment ref) {
        try {
            dexpp_class_ref_free.invokeExact(ref);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_ref_array_free =
            downcallVoid("dexpp_class_ref_array_free", ADDRESS, JAVA_LONG);

    static void dexpp_class_ref_array_free(MemorySegment refs, long count) {
        try {
            dexpp_class_ref_array_free.invokeExact(refs, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_method_name =
            downcall("dexpp_method_name", ADDRESS, ADDRESS);

    static MemorySegment dexpp_method_name(MemorySegment m) {
        try {
            return (MemorySegment) dexpp_method_name.invokeExact(m);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_method_descriptor =
            downcall("dexpp_method_descriptor", ADDRESS, ADDRESS);

    static MemorySegment dexpp_method_descriptor(MemorySegment m) {
        try {
            return (MemorySegment) dexpp_method_descriptor.invokeExact(m);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_method_declaring_class =
            downcall("dexpp_method_declaring_class", ADDRESS, ADDRESS);

    static MemorySegment dexpp_method_declaring_class(MemorySegment m) {
        try {
            return (MemorySegment) dexpp_method_declaring_class.invokeExact(m);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_method_return_type =
            downcall("dexpp_method_return_type", ADDRESS, ADDRESS);

    static MemorySegment dexpp_method_return_type(MemorySegment m) {
        try {
            return (MemorySegment) dexpp_method_return_type.invokeExact(m);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_method_parameters =
            downcall("dexpp_method_parameters", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_method_parameters(MemorySegment m, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_method_parameters.invokeExact(m, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_method_access_flags =
            downcall("dexpp_method_access_flags", JAVA_INT, ADDRESS);

    static int dexpp_method_access_flags(MemorySegment m) {
        try {
            return (int) dexpp_method_access_flags.invokeExact(m);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_method_has_code =
            downcall("dexpp_method_has_code", JAVA_INT, ADDRESS);

    static int dexpp_method_has_code(MemorySegment m) {
        try {
            return (int) dexpp_method_has_code.invokeExact(m);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_method_is_static =
            downcall("dexpp_method_is_static", JAVA_INT, ADDRESS);

    static int dexpp_method_is_static(MemorySegment m) {
        try {
            return (int) dexpp_method_is_static.invokeExact(m);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_method_is_constructor =
            downcall("dexpp_method_is_constructor", JAVA_INT, ADDRESS);

    static int dexpp_method_is_constructor(MemorySegment m) {
        try {
            return (int) dexpp_method_is_constructor.invokeExact(m);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_method_register_count =
            downcall("dexpp_method_register_count", JAVA_SHORT, ADDRESS);

    static short dexpp_method_register_count(MemorySegment m) {
        try {
            return (short) dexpp_method_register_count.invokeExact(m);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_method_parameter_registers =
            downcall("dexpp_method_parameter_registers", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_method_parameter_registers(MemorySegment m, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_method_parameter_registers.invokeExact(m, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_method_calls =
            downcall("dexpp_method_calls", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_method_calls(MemorySegment m, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_method_calls.invokeExact(m, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_method_field_accesses =
            downcall("dexpp_method_field_accesses", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_method_field_accesses(MemorySegment m, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_method_field_accesses.invokeExact(m, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_method_string_loads =
            downcall("dexpp_method_string_loads", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_method_string_loads(MemorySegment m, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_method_string_loads.invokeExact(m, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_method_type_uses =
            downcall("dexpp_method_type_uses", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_method_type_uses(MemorySegment m, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_method_type_uses.invokeExact(m, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_method_try_blocks =
            downcall("dexpp_method_try_blocks", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_method_try_blocks(MemorySegment m, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_method_try_blocks.invokeExact(m, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_method_annotations =
            downcall("dexpp_method_annotations", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_method_annotations(MemorySegment m, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_method_annotations.invokeExact(m, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_method_free =
            downcallVoid("dexpp_method_free", ADDRESS);

    static void dexpp_method_free(MemorySegment m) {
        try {
            dexpp_method_free.invokeExact(m);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_method_array_free =
            downcallVoid("dexpp_method_array_free", ADDRESS, JAVA_LONG);

    static void dexpp_method_array_free(MemorySegment methods, long count) {
        try {
            dexpp_method_array_free.invokeExact(methods, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_parameter_register_array_free =
            downcallVoid("dexpp_parameter_register_array_free", ADDRESS, JAVA_LONG);

    static void dexpp_parameter_register_array_free(MemorySegment regs, long count) {
        try {
            dexpp_parameter_register_array_free.invokeExact(regs, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_method_call_array_free =
            downcallVoid("dexpp_method_call_array_free", ADDRESS, JAVA_LONG);

    static void dexpp_method_call_array_free(MemorySegment calls, long count) {
        try {
            dexpp_method_call_array_free.invokeExact(calls, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_field_access_array_free =
            downcallVoid("dexpp_field_access_array_free", ADDRESS, JAVA_LONG);

    static void dexpp_field_access_array_free(MemorySegment accesses, long count) {
        try {
            dexpp_field_access_array_free.invokeExact(accesses, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_string_load_array_free =
            downcallVoid("dexpp_string_load_array_free", ADDRESS, JAVA_LONG);

    static void dexpp_string_load_array_free(MemorySegment loads, long count) {
        try {
            dexpp_string_load_array_free.invokeExact(loads, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_type_use_array_free =
            downcallVoid("dexpp_type_use_array_free", ADDRESS, JAVA_LONG);

    static void dexpp_type_use_array_free(MemorySegment uses, long count) {
        try {
            dexpp_type_use_array_free.invokeExact(uses, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_try_block_array_free =
            downcallVoid("dexpp_try_block_array_free", ADDRESS, JAVA_LONG);

    static void dexpp_try_block_array_free(MemorySegment blocks, long count) {
        try {
            dexpp_try_block_array_free.invokeExact(blocks, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_field_name =
            downcall("dexpp_field_name", ADDRESS, ADDRESS);

    static MemorySegment dexpp_field_name(MemorySegment f) {
        try {
            return (MemorySegment) dexpp_field_name.invokeExact(f);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_field_declaring_class =
            downcall("dexpp_field_declaring_class", ADDRESS, ADDRESS);

    static MemorySegment dexpp_field_declaring_class(MemorySegment f) {
        try {
            return (MemorySegment) dexpp_field_declaring_class.invokeExact(f);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_field_type =
            downcall("dexpp_field_type", ADDRESS, ADDRESS);

    static MemorySegment dexpp_field_type(MemorySegment f) {
        try {
            return (MemorySegment) dexpp_field_type.invokeExact(f);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_field_access_flags =
            downcall("dexpp_field_access_flags", JAVA_INT, ADDRESS);

    static int dexpp_field_access_flags(MemorySegment f) {
        try {
            return (int) dexpp_field_access_flags.invokeExact(f);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_field_annotations =
            downcall("dexpp_field_annotations", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_field_annotations(MemorySegment f, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_field_annotations.invokeExact(f, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_field_initial_value =
            downcall("dexpp_field_initial_value", JAVA_INT, ADDRESS, ADDRESS);

    static int dexpp_field_initial_value(MemorySegment f, MemorySegment out) {
        try {
            return (int) dexpp_field_initial_value.invokeExact(f, out);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_field_free = downcallVoid("dexpp_field_free", ADDRESS);

    static void dexpp_field_free(MemorySegment f) {
        try {
            dexpp_field_free.invokeExact(f);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_field_array_free =
            downcallVoid("dexpp_field_array_free", ADDRESS, JAVA_LONG);

    static void dexpp_field_array_free(MemorySegment fields, long count) {
        try {
            dexpp_field_array_free.invokeExact(fields, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_method_instructions =
            downcall("dexpp_method_instructions", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_method_instructions(MemorySegment m, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_method_instructions.invokeExact(m, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_instruction_array_free =
            downcallVoid("dexpp_instruction_array_free", ADDRESS, JAVA_LONG);

    static void dexpp_instruction_array_free(MemorySegment insns, long count) {
        try {
            dexpp_instruction_array_free.invokeExact(insns, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_opcode_name =
            downcall("dexpp_opcode_name", ADDRESS, JAVA_BYTE);

    static MemorySegment dexpp_opcode_name(byte opcode) {
        try {
            return (MemorySegment) dexpp_opcode_name.invokeExact(opcode);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_instruction_to_string =
            downcall("dexpp_instruction_to_string", ADDRESS, ADDRESS);

    static MemorySegment dexpp_instruction_to_string(MemorySegment insn) {
        try {
            return (MemorySegment) dexpp_instruction_to_string.invokeExact(insn);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_method_cfg =
            downcall("dexpp_method_cfg", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_method_cfg(MemorySegment m, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_method_cfg.invokeExact(m, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_basic_block_array_free =
            downcallVoid("dexpp_basic_block_array_free", ADDRESS, JAVA_LONG);

    static void dexpp_basic_block_array_free(MemorySegment blocks, long count) {
        try {
            dexpp_basic_block_array_free.invokeExact(blocks, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_call_graph_node_count =
            downcall("dexpp_call_graph_node_count", JAVA_LONG, ADDRESS);

    static long dexpp_call_graph_node_count(MemorySegment g) {
        try {
            return (long) dexpp_call_graph_node_count.invokeExact(g);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_call_graph_edge_count =
            downcall("dexpp_call_graph_edge_count", JAVA_LONG, ADDRESS);

    static long dexpp_call_graph_edge_count(MemorySegment g) {
        try {
            return (long) dexpp_call_graph_edge_count.invokeExact(g);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_call_graph_nodes =
            downcall("dexpp_call_graph_nodes", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_call_graph_nodes(MemorySegment g, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_call_graph_nodes.invokeExact(g, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_call_graph_node =
            downcall("dexpp_call_graph_node", JAVA_INT, ADDRESS, JAVA_INT, ADDRESS);

    static int dexpp_call_graph_node(MemorySegment g, int node, MemorySegment out) {
        try {
            return (int) dexpp_call_graph_node.invokeExact(g, node, out);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_call_graph_edges =
            downcall("dexpp_call_graph_edges", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_call_graph_edges(MemorySegment g, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_call_graph_edges.invokeExact(g, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_call_graph_edge =
            downcall("dexpp_call_graph_edge", JAVA_INT, ADDRESS, JAVA_INT, ADDRESS);

    static int dexpp_call_graph_edge(MemorySegment g, int edge, MemorySegment out) {
        try {
            return (int) dexpp_call_graph_edge.invokeExact(g, edge, out);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_call_graph_find =
            downcall("dexpp_call_graph_find", JAVA_LONG, ADDRESS, ADDRESS, ADDRESS, ADDRESS);

    static long dexpp_call_graph_find(
            MemorySegment g,
            MemorySegment classDescriptor,
            MemorySegment name,
            MemorySegment proto) {
        try {
            return (long) dexpp_call_graph_find.invokeExact(g, classDescriptor, name, proto);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_call_graph_out_edges =
            downcall("dexpp_call_graph_out_edges", ADDRESS, ADDRESS, JAVA_INT, ADDRESS);

    static MemorySegment dexpp_call_graph_out_edges(
            MemorySegment g, int node, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_call_graph_out_edges.invokeExact(g, node, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_call_graph_in_edges =
            downcall("dexpp_call_graph_in_edges", ADDRESS, ADDRESS, JAVA_INT, ADDRESS);

    static MemorySegment dexpp_call_graph_in_edges(MemorySegment g, int node, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_call_graph_in_edges.invokeExact(g, node, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_call_graph_dispatch_targets =
            downcall("dexpp_call_graph_dispatch_targets", ADDRESS, ADDRESS, JAVA_INT, ADDRESS);

    static MemorySegment dexpp_call_graph_dispatch_targets(
            MemorySegment g, int node, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_call_graph_dispatch_targets.invokeExact(g, node, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_call_graph_dispatch_sources =
            downcall("dexpp_call_graph_dispatch_sources", ADDRESS, ADDRESS, JAVA_INT, ADDRESS);

    static MemorySegment dexpp_call_graph_dispatch_sources(
            MemorySegment g, int node, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_call_graph_dispatch_sources.invokeExact(g, node, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_call_graph_callees =
            downcall("dexpp_call_graph_callees", ADDRESS, ADDRESS, JAVA_INT, ADDRESS);

    static MemorySegment dexpp_call_graph_callees(MemorySegment g, int node, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_call_graph_callees.invokeExact(g, node, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_call_graph_callers =
            downcall("dexpp_call_graph_callers", ADDRESS, ADDRESS, JAVA_INT, ADDRESS);

    static MemorySegment dexpp_call_graph_callers(MemorySegment g, int node, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_call_graph_callers.invokeExact(g, node, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_call_graph_edge_dispatch_targets =
            downcall("dexpp_call_graph_edge_dispatch_targets", ADDRESS, ADDRESS, JAVA_INT, ADDRESS);

    static MemorySegment dexpp_call_graph_edge_dispatch_targets(
            MemorySegment g, int edge, MemorySegment count) {
        try {
            return (MemorySegment)
                    dexpp_call_graph_edge_dispatch_targets.invokeExact(g, edge, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_call_graph_edges_where =
            downcall("dexpp_call_graph_edges_where", ADDRESS, ADDRESS, JAVA_INT, JAVA_INT, ADDRESS);

    static MemorySegment dexpp_call_graph_edges_where(
            MemorySegment g, int origin, int kind, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_call_graph_edges_where.invokeExact(g, origin, kind, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_call_graph_method_for =
            downcall("dexpp_call_graph_method_for", ADDRESS, ADDRESS, JAVA_INT);

    static MemorySegment dexpp_call_graph_method_for(MemorySegment g, int node) {
        try {
            return (MemorySegment) dexpp_call_graph_method_for.invokeExact(g, node);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_call_graph_free =
            downcallVoid("dexpp_call_graph_free", ADDRESS);

    static void dexpp_call_graph_free(MemorySegment g) {
        try {
            dexpp_call_graph_free.invokeExact(g);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_call_node_clear =
            downcallVoid("dexpp_call_node_clear", ADDRESS);

    static void dexpp_call_node_clear(MemorySegment node) {
        try {
            dexpp_call_node_clear.invokeExact(node);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_call_node_array_free =
            downcallVoid("dexpp_call_node_array_free", ADDRESS, JAVA_LONG);

    static void dexpp_call_node_array_free(MemorySegment nodes, long count) {
        try {
            dexpp_call_node_array_free.invokeExact(nodes, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_call_edge_array_free =
            downcallVoid("dexpp_call_edge_array_free", ADDRESS, JAVA_LONG);

    static void dexpp_call_edge_array_free(MemorySegment edges, long count) {
        try {
            dexpp_call_edge_array_free.invokeExact(edges, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_hierarchy_supertypes =
            downcall("dexpp_class_hierarchy_supertypes", ADDRESS, ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_class_hierarchy_supertypes(
            MemorySegment h, MemorySegment descriptor, MemorySegment count) {
        try {
            return (MemorySegment)
                    dexpp_class_hierarchy_supertypes.invokeExact(h, descriptor, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_hierarchy_superclass_of =
            downcall("dexpp_class_hierarchy_superclass_of", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_class_hierarchy_superclass_of(
            MemorySegment h, MemorySegment descriptor) {
        try {
            return (MemorySegment) dexpp_class_hierarchy_superclass_of.invokeExact(h, descriptor);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_hierarchy_direct_interfaces =
            downcall("dexpp_class_hierarchy_direct_interfaces", ADDRESS, ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_class_hierarchy_direct_interfaces(
            MemorySegment h, MemorySegment descriptor, MemorySegment count) {
        try {
            return (MemorySegment)
                    dexpp_class_hierarchy_direct_interfaces.invokeExact(h, descriptor, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_hierarchy_subclasses =
            downcall("dexpp_class_hierarchy_subclasses", ADDRESS, ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_class_hierarchy_subclasses(
            MemorySegment h, MemorySegment descriptor, MemorySegment count) {
        try {
            return (MemorySegment)
                    dexpp_class_hierarchy_subclasses.invokeExact(h, descriptor, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_hierarchy_implementers =
            downcall("dexpp_class_hierarchy_implementers", ADDRESS, ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_class_hierarchy_implementers(
            MemorySegment h, MemorySegment descriptor, MemorySegment count) {
        try {
            return (MemorySegment)
                    dexpp_class_hierarchy_implementers.invokeExact(h, descriptor, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_hierarchy_all_supertypes =
            downcall("dexpp_class_hierarchy_all_supertypes", ADDRESS, ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_class_hierarchy_all_supertypes(
            MemorySegment h, MemorySegment descriptor, MemorySegment count) {
        try {
            return (MemorySegment)
                    dexpp_class_hierarchy_all_supertypes.invokeExact(h, descriptor, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_hierarchy_all_descendants =
            downcall("dexpp_class_hierarchy_all_descendants", ADDRESS, ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_class_hierarchy_all_descendants(
            MemorySegment h, MemorySegment descriptor, MemorySegment count) {
        try {
            return (MemorySegment)
                    dexpp_class_hierarchy_all_descendants.invokeExact(h, descriptor, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_hierarchy_is_subtype_of =
            downcall("dexpp_class_hierarchy_is_subtype_of", JAVA_INT, ADDRESS, ADDRESS, ADDRESS);

    static int dexpp_class_hierarchy_is_subtype_of(
            MemorySegment h, MemorySegment sub, MemorySegment superDescriptor) {
        try {
            return (int) dexpp_class_hierarchy_is_subtype_of.invokeExact(h, sub, superDescriptor);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_hierarchy_is_loaded =
            downcall("dexpp_class_hierarchy_is_loaded", JAVA_INT, ADDRESS, ADDRESS);

    static int dexpp_class_hierarchy_is_loaded(MemorySegment h, MemorySegment descriptor) {
        try {
            return (int) dexpp_class_hierarchy_is_loaded.invokeExact(h, descriptor);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_hierarchy_is_interface =
            downcall("dexpp_class_hierarchy_is_interface", JAVA_INT, ADDRESS, ADDRESS);

    static int dexpp_class_hierarchy_is_interface(MemorySegment h, MemorySegment descriptor) {
        try {
            return (int) dexpp_class_hierarchy_is_interface.invokeExact(h, descriptor);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_hierarchy_empty =
            downcall("dexpp_class_hierarchy_empty", JAVA_INT, ADDRESS);

    static int dexpp_class_hierarchy_empty(MemorySegment h) {
        try {
            return (int) dexpp_class_hierarchy_empty.invokeExact(h);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_hierarchy_free =
            downcallVoid("dexpp_class_hierarchy_free", ADDRESS);

    static void dexpp_class_hierarchy_free(MemorySegment h) {
        try {
            dexpp_class_hierarchy_free.invokeExact(h);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_xrefs_referrer_count =
            downcall("dexpp_xrefs_referrer_count", JAVA_LONG, ADDRESS);

    static long dexpp_xrefs_referrer_count(MemorySegment x) {
        try {
            return (long) dexpp_xrefs_referrer_count.invokeExact(x);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_xrefs_referrers =
            downcall("dexpp_xrefs_referrers", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_xrefs_referrers(MemorySegment x, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_xrefs_referrers.invokeExact(x, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_xrefs_referrer_id =
            downcall("dexpp_xrefs_referrer_id", JAVA_INT, ADDRESS, JAVA_INT, ADDRESS);

    static int dexpp_xrefs_referrer_id(MemorySegment x, int referrer, MemorySegment out) {
        try {
            return (int) dexpp_xrefs_referrer_id.invokeExact(x, referrer, out);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_xrefs_referrer_method =
            downcall("dexpp_xrefs_referrer_method", ADDRESS, ADDRESS, JAVA_INT);

    static MemorySegment dexpp_xrefs_referrer_method(MemorySegment x, int referrer) {
        try {
            return (MemorySegment) dexpp_xrefs_referrer_method.invokeExact(x, referrer);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_xrefs_method_refs =
            downcall(
                    "dexpp_xrefs_method_refs",
                    ADDRESS,
                    ADDRESS,
                    ADDRESS,
                    ADDRESS,
                    ADDRESS,
                    ADDRESS);

    static MemorySegment dexpp_xrefs_method_refs(
            MemorySegment x,
            MemorySegment classDescriptor,
            MemorySegment name,
            MemorySegment proto,
            MemorySegment count) {
        try {
            return (MemorySegment)
                    dexpp_xrefs_method_refs.invokeExact(x, classDescriptor, name, proto, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_xrefs_method_refs_by_name =
            downcall("dexpp_xrefs_method_refs_by_name", ADDRESS, ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_xrefs_method_refs_by_name(
            MemorySegment x, MemorySegment name, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_xrefs_method_refs_by_name.invokeExact(x, name, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_xrefs_field_refs =
            downcall(
                    "dexpp_xrefs_field_refs", ADDRESS, ADDRESS, ADDRESS, ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_xrefs_field_refs(
            MemorySegment x,
            MemorySegment classDescriptor,
            MemorySegment name,
            MemorySegment type,
            MemorySegment count) {
        try {
            return (MemorySegment)
                    dexpp_xrefs_field_refs.invokeExact(x, classDescriptor, name, type, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_xrefs_field_reads =
            downcall(
                    "dexpp_xrefs_field_reads",
                    ADDRESS,
                    ADDRESS,
                    ADDRESS,
                    ADDRESS,
                    ADDRESS,
                    ADDRESS);

    static MemorySegment dexpp_xrefs_field_reads(
            MemorySegment x,
            MemorySegment classDescriptor,
            MemorySegment name,
            MemorySegment type,
            MemorySegment count) {
        try {
            return (MemorySegment)
                    dexpp_xrefs_field_reads.invokeExact(x, classDescriptor, name, type, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_xrefs_field_writes =
            downcall(
                    "dexpp_xrefs_field_writes",
                    ADDRESS,
                    ADDRESS,
                    ADDRESS,
                    ADDRESS,
                    ADDRESS,
                    ADDRESS);

    static MemorySegment dexpp_xrefs_field_writes(
            MemorySegment x,
            MemorySegment classDescriptor,
            MemorySegment name,
            MemorySegment type,
            MemorySegment count) {
        try {
            return (MemorySegment)
                    dexpp_xrefs_field_writes.invokeExact(x, classDescriptor, name, type, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_xrefs_string_refs =
            downcall("dexpp_xrefs_string_refs", ADDRESS, ADDRESS, ADDRESS, JAVA_LONG, ADDRESS);

    static MemorySegment dexpp_xrefs_string_refs(
            MemorySegment x, MemorySegment value, long length, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_xrefs_string_refs.invokeExact(x, value, length, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_xrefs_referenced_strings =
            downcall("dexpp_xrefs_referenced_strings", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_xrefs_referenced_strings(MemorySegment x, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_xrefs_referenced_strings.invokeExact(x, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_xrefs_type_refs =
            downcall("dexpp_xrefs_type_refs", ADDRESS, ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_xrefs_type_refs(
            MemorySegment x, MemorySegment descriptor, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_xrefs_type_refs.invokeExact(x, descriptor, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_xrefs_empty =
            downcall("dexpp_xrefs_empty", JAVA_INT, ADDRESS);

    static int dexpp_xrefs_empty(MemorySegment x) {
        try {
            return (int) dexpp_xrefs_empty.invokeExact(x);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_xrefs_free = downcallVoid("dexpp_xrefs_free", ADDRESS);

    static void dexpp_xrefs_free(MemorySegment x) {
        try {
            dexpp_xrefs_free.invokeExact(x);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_method_xref_array_free =
            downcallVoid("dexpp_method_xref_array_free", ADDRESS, JAVA_LONG);

    static void dexpp_method_xref_array_free(MemorySegment xrefs, long count) {
        try {
            dexpp_method_xref_array_free.invokeExact(xrefs, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_method_ref_group_array_free =
            downcallVoid("dexpp_method_ref_group_array_free", ADDRESS, JAVA_LONG);

    static void dexpp_method_ref_group_array_free(MemorySegment groups, long count) {
        try {
            dexpp_method_ref_group_array_free.invokeExact(groups, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_field_xref_array_free =
            downcallVoid("dexpp_field_xref_array_free", ADDRESS, JAVA_LONG);

    static void dexpp_field_xref_array_free(MemorySegment xrefs, long count) {
        try {
            dexpp_field_xref_array_free.invokeExact(xrefs, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_string_xref_array_free =
            downcallVoid("dexpp_string_xref_array_free", ADDRESS, JAVA_LONG);

    static void dexpp_string_xref_array_free(MemorySegment xrefs, long count) {
        try {
            dexpp_string_xref_array_free.invokeExact(xrefs, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_type_xref_array_free =
            downcallVoid("dexpp_type_xref_array_free", ADDRESS, JAVA_LONG);

    static void dexpp_type_xref_array_free(MemorySegment xrefs, long count) {
        try {
            dexpp_type_xref_array_free.invokeExact(xrefs, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_apk_open =
            downcall("dexpp_apk_open", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_apk_open(MemorySegment path, MemorySegment err) {
        try {
            return (MemorySegment) dexpp_apk_open.invokeExact(path, err);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_apk_free = downcallVoid("dexpp_apk_free", ADDRESS);

    static void dexpp_apk_free(MemorySegment apk) {
        try {
            dexpp_apk_free.invokeExact(apk);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_apk_entries =
            downcall("dexpp_apk_entries", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_apk_entries(MemorySegment apk, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_apk_entries.invokeExact(apk, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_apk_read =
            downcall("dexpp_apk_read", JAVA_INT, ADDRESS, ADDRESS, ADDRESS, ADDRESS);

    static int dexpp_apk_read(
            MemorySegment apk, MemorySegment name, MemorySegment out, MemorySegment length) {
        try {
            return (int) dexpp_apk_read.invokeExact(apk, name, out, length);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_apk_package =
            downcall("dexpp_apk_package", ADDRESS, ADDRESS);

    static MemorySegment dexpp_apk_package(MemorySegment apk) {
        try {
            return (MemorySegment) dexpp_apk_package.invokeExact(apk);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_apk_manifest =
            downcall("dexpp_apk_manifest", JAVA_INT, ADDRESS, ADDRESS);

    static int dexpp_apk_manifest(MemorySegment apk, MemorySegment out) {
        try {
            return (int) dexpp_apk_manifest.invokeExact(apk, out);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_manifest_clear =
            downcallVoid("dexpp_manifest_clear", ADDRESS);

    static void dexpp_manifest_clear(MemorySegment manifest) {
        try {
            dexpp_manifest_clear.invokeExact(manifest);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_apk_has_resources =
            downcall("dexpp_apk_has_resources", JAVA_INT, ADDRESS);

    static int dexpp_apk_has_resources(MemorySegment apk) {
        try {
            return (int) dexpp_apk_has_resources.invokeExact(apk);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_apk_resources_empty =
            downcall("dexpp_apk_resources_empty", JAVA_INT, ADDRESS);

    static int dexpp_apk_resources_empty(MemorySegment apk) {
        try {
            return (int) dexpp_apk_resources_empty.invokeExact(apk);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_apk_resource_name =
            downcall("dexpp_apk_resource_name", JAVA_INT, ADDRESS, JAVA_INT, ADDRESS);

    static int dexpp_apk_resource_name(MemorySegment apk, int id, MemorySegment out) {
        try {
            return (int) dexpp_apk_resource_name.invokeExact(apk, id, out);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_resource_name_clear =
            downcallVoid("dexpp_resource_name_clear", ADDRESS);

    static void dexpp_resource_name_clear(MemorySegment name) {
        try {
            dexpp_resource_name_clear.invokeExact(name);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_apk_resolve_string =
            downcall("dexpp_apk_resolve_string", JAVA_INT, ADDRESS, JAVA_INT, ADDRESS);

    static int dexpp_apk_resolve_string(MemorySegment apk, int id, MemorySegment out) {
        try {
            return (int) dexpp_apk_resolve_string.invokeExact(apk, id, out);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_apk_resource_id =
            downcall("dexpp_apk_resource_id", JAVA_INT, ADDRESS, ADDRESS, ADDRESS, ADDRESS);

    static int dexpp_apk_resource_id(
            MemorySegment apk, MemorySegment type, MemorySegment entry, MemorySegment out) {
        try {
            return (int) dexpp_apk_resource_id.invokeExact(apk, type, entry, out);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_apk_signing =
            downcallVoid("dexpp_apk_signing", ADDRESS, ADDRESS);

    static void dexpp_apk_signing(MemorySegment apk, MemorySegment out) {
        try {
            dexpp_apk_signing.invokeExact(apk, out);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_signing_info_clear =
            downcallVoid("dexpp_signing_info_clear", ADDRESS);

    static void dexpp_signing_info_clear(MemorySegment info) {
        try {
            dexpp_signing_info_clear.invokeExact(info);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_apk_analysis =
            downcall("dexpp_apk_analysis", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_apk_analysis(MemorySegment apk, MemorySegment err) {
        try {
            return (MemorySegment) dexpp_apk_analysis.invokeExact(apk, err);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    // ---- dexpp_model.h -------------------------------------------------------

    private static final MethodHandle dexpp_model_read =
            downcall("dexpp_model_read", ADDRESS, ADDRESS, JAVA_LONG, JAVA_INT, ADDRESS);

    static MemorySegment dexpp_model_read(
            MemorySegment bytes, long len, int debugInfo, MemorySegment err) {
        try {
            return (MemorySegment) dexpp_model_read.invokeExact(bytes, len, debugInfo, err);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_model_read_file =
            downcall("dexpp_model_read_file", ADDRESS, ADDRESS, JAVA_INT, ADDRESS);

    static MemorySegment dexpp_model_read_file(
            MemorySegment path, int debugInfo, MemorySegment err) {
        try {
            return (MemorySegment) dexpp_model_read_file.invokeExact(path, debugInfo, err);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_model_new =
            downcall("dexpp_model_new", ADDRESS, JAVA_INT);

    static MemorySegment dexpp_model_new(int version) {
        try {
            return (MemorySegment) dexpp_model_new.invokeExact(version);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_model_clone =
            downcall("dexpp_model_clone", ADDRESS, ADDRESS);

    static MemorySegment dexpp_model_clone(MemorySegment dex) {
        try {
            return (MemorySegment) dexpp_model_clone.invokeExact(dex);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_model_free = downcallVoid("dexpp_model_free", ADDRESS);

    static void dexpp_model_free(MemorySegment dex) {
        try {
            dexpp_model_free.invokeExact(dex);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_model_write =
            downcall("dexpp_model_write", ADDRESS, ADDRESS, JAVA_INT, ADDRESS, ADDRESS);

    static MemorySegment dexpp_model_write(
            MemorySegment dex, int debugInfo, MemorySegment outLen, MemorySegment err) {
        try {
            return (MemorySegment) dexpp_model_write.invokeExact(dex, debugInfo, outLen, err);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_model_write_file =
            downcall("dexpp_model_write_file", JAVA_INT, ADDRESS, ADDRESS, JAVA_INT, ADDRESS);

    static int dexpp_model_write_file(
            MemorySegment dex, MemorySegment path, int debugInfo, MemorySegment err) {
        try {
            return (int) dexpp_model_write_file.invokeExact(dex, path, debugInfo, err);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_model_version =
            downcall("dexpp_model_version", JAVA_INT, ADDRESS);

    static int dexpp_model_version(MemorySegment dex) {
        try {
            return (int) dexpp_model_version.invokeExact(dex);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_model_set_version =
            downcallVoid("dexpp_model_set_version", ADDRESS, JAVA_INT);

    static void dexpp_model_set_version(MemorySegment dex, int version) {
        try {
            dexpp_model_set_version.invokeExact(dex, version);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_model_required_version =
            downcall("dexpp_model_required_version", JAVA_INT, ADDRESS);

    static int dexpp_model_required_version(MemorySegment dex) {
        try {
            return (int) dexpp_model_required_version.invokeExact(dex);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_model_pool_stats =
            downcall("dexpp_model_pool_stats", Layouts.PoolStats.LAYOUT, ADDRESS);

    static MemorySegment dexpp_model_pool_stats(SegmentAllocator allocator, MemorySegment dex) {
        try {
            return (MemorySegment) dexpp_model_pool_stats.invokeExact(allocator, dex);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_model_validate =
            downcall("dexpp_model_validate", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_model_validate(MemorySegment dex, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_model_validate.invokeExact(dex, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_problem_array_free =
            downcallVoid("dexpp_problem_array_free", ADDRESS, JAVA_LONG);

    static void dexpp_problem_array_free(MemorySegment problems, long count) {
        try {
            dexpp_problem_array_free.invokeExact(problems, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_model_class_count =
            downcall("dexpp_model_class_count", JAVA_LONG, ADDRESS);

    static long dexpp_model_class_count(MemorySegment dex) {
        try {
            return (long) dexpp_model_class_count.invokeExact(dex);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_model_class_descriptors =
            downcall("dexpp_model_class_descriptors", ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_model_class_descriptors(MemorySegment dex, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_model_class_descriptors.invokeExact(dex, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_model_has_class =
            downcall("dexpp_model_has_class", JAVA_INT, ADDRESS, ADDRESS);

    static int dexpp_model_has_class(MemorySegment dex, MemorySegment descriptor) {
        try {
            return (int) dexpp_model_has_class.invokeExact(dex, descriptor);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_model_class_info =
            downcall("dexpp_model_class_info", JAVA_INT, ADDRESS, ADDRESS, ADDRESS);

    static int dexpp_model_class_info(
            MemorySegment dex, MemorySegment descriptor, MemorySegment out) {
        try {
            return (int) dexpp_model_class_info.invokeExact(dex, descriptor, out);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_class_info_clear =
            downcallVoid("dexpp_class_info_clear", ADDRESS);

    static void dexpp_class_info_clear(MemorySegment info) {
        try {
            dexpp_class_info_clear.invokeExact(info);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_model_class_interfaces =
            downcall("dexpp_model_class_interfaces", ADDRESS, ADDRESS, ADDRESS, ADDRESS);

    static MemorySegment dexpp_model_class_interfaces(
            MemorySegment dex, MemorySegment descriptor, MemorySegment count) {
        try {
            return (MemorySegment) dexpp_model_class_interfaces.invokeExact(dex, descriptor, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_model_move_class =
            downcall("dexpp_model_move_class", JAVA_INT, ADDRESS, ADDRESS, ADDRESS, ADDRESS);

    static int dexpp_model_move_class(
            MemorySegment dst, MemorySegment src, MemorySegment descriptor, MemorySegment err) {
        try {
            return (int) dexpp_model_move_class.invokeExact(dst, src, descriptor, err);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_model_copy_class =
            downcall("dexpp_model_copy_class", JAVA_INT, ADDRESS, ADDRESS, ADDRESS, ADDRESS);

    static int dexpp_model_copy_class(
            MemorySegment dst, MemorySegment src, MemorySegment descriptor, MemorySegment err) {
        try {
            return (int) dexpp_model_copy_class.invokeExact(dst, src, descriptor, err);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_model_remove_class =
            downcall("dexpp_model_remove_class", JAVA_INT, ADDRESS, ADDRESS);

    static int dexpp_model_remove_class(MemorySegment dex, MemorySegment descriptor) {
        try {
            return (int) dexpp_model_remove_class.invokeExact(dex, descriptor);
        } catch (Throwable t) {
            throw failure(t);
        }
    }

    private static final MethodHandle dexpp_model_rename_types =
            downcallVoid("dexpp_model_rename_types", ADDRESS, ADDRESS, ADDRESS, JAVA_LONG);

    static void dexpp_model_rename_types(
            MemorySegment dex, MemorySegment from, MemorySegment to, long count) {
        try {
            dexpp_model_rename_types.invokeExact(dex, from, to, count);
        } catch (Throwable t) {
            throw failure(t);
        }
    }
}
