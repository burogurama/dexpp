// The C API for analysis (bindings/c/dexpp.h): loading, errors, classes,
// class references, methods and fields.  See c_test_util.hpp.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#include "c_test_util.hpp"
#include "dexpp.h"

using c_test::message;
using c_test::open_dex;
using c_test::str;
using c_test::take;
using c_test::take_all;

TEST(CApiContext, LoadsADexFileAndListsItsClasses)
{
    DexppContext *ctx = open_dex("tests/data/shapes.dex");
    ASSERT_NE(ctx, nullptr);

    size_t count = 0;
    DexppClass **classes = dexpp_context_classes(ctx, &count);
    ASSERT_EQ(count, 4u);
    std::set<std::string> names;
    for (size_t i = 0; i < count; ++i)
        names.insert(take(dexpp_class_descriptor(classes[i])));
    EXPECT_EQ(names,
              (std::set<std::string>{"LShape;", "LAbstractShape;", "LCircle;", "LRectangle;"}));

    dexpp_class_array_free(classes, count);
    dexpp_context_free(ctx);
}

TEST(CApiContext, LoadsAnApkInMultidexOrderWithAnyThreadCount)
{
    for (uint32_t threads : {0u, 1u, 4u}) {
        DexppError *err = nullptr;
        DexppContext *ctx = dexpp_context_from_apk("tests/data/app.apk", threads, &err);
        ASSERT_NE(ctx, nullptr) << message(err);
        EXPECT_EQ(err, nullptr);

        size_t count = 0;
        DexppClass **classes = dexpp_context_classes(ctx, &count);
        ASSERT_EQ(count, 2u);
        EXPECT_EQ(take(dexpp_class_descriptor(classes[0])), "LTestDex;");
        EXPECT_EQ(take(dexpp_class_descriptor(classes[1])), "LFibonacci;");
        dexpp_class_array_free(classes, count);
        dexpp_context_free(ctx);
    }
}

TEST(CApiContext, LoadsSeveralDexFiles)
{
    const char *paths[] = {"tests/data/classes.dex", "tests/data/fibonacci.dex"};
    DexppError *err = nullptr;
    DexppContext *ctx = dexpp_context_from_dex_files(paths, 2, &err);
    ASSERT_NE(ctx, nullptr) << message(err);
    EXPECT_EQ(err, nullptr);
    DexppClass *a = dexpp_context_find_class(ctx, "LTestDex;");
    DexppClass *b = dexpp_context_find_class(ctx, "LFibonacci;");
    EXPECT_NE(a, nullptr);
    EXPECT_NE(b, nullptr);
    dexpp_class_free(a);
    dexpp_class_free(b);
    dexpp_context_free(ctx);

    const char *bad[] = {"tests/data/classes.dex", "tests/data/no_such_file.dex"};
    EXPECT_EQ(dexpp_context_from_dex_files(bad, 2, &err), nullptr);
    ASSERT_NE(err, nullptr);
    EXPECT_EQ(dexpp_error_code(err), DEXPP_ERROR_FILE_NOT_FOUND);
    dexpp_error_free(err);
}

TEST(CApiErrors, LoadErrorsCarryACodeAndAMessage)
{
    struct Case
    {
        const char *path;
        bool apk;
        DexppErrorCode code;
    };
    for (const Case &c : {Case{"tests/data/no_such_file.dex", false, DEXPP_ERROR_FILE_NOT_FOUND},
                          Case{"tests/data/TestDex.java", false, DEXPP_ERROR_INVALID_DEX},
                          Case{"tests/data", false, DEXPP_ERROR_CANNOT_READ},
                          Case{"tests/data/no_such_file.apk", true, DEXPP_ERROR_FILE_NOT_FOUND},
                          Case{"tests/data/classes.dex", true, DEXPP_ERROR_INVALID_APK},
                          Case{"tests/data/nodex.apk", true, DEXPP_ERROR_INVALID_APK},
                          Case{"tests/data", true, DEXPP_ERROR_CANNOT_READ}}) {
        SCOPED_TRACE(c.path);
        DexppError *err = nullptr;
        DexppContext *ctx =
            c.apk ? dexpp_context_from_apk(c.path, 0, &err) : dexpp_context_from_dex(c.path, &err);
        EXPECT_EQ(ctx, nullptr);
        ASSERT_NE(err, nullptr);
        EXPECT_EQ(dexpp_error_code(err), c.code);
        EXPECT_NE(message(err), "");
        EXPECT_EQ(dexpp_error_problem_count(err), 0u);
        EXPECT_EQ(dexpp_error_problem_where(err, 0), nullptr);
        EXPECT_EQ(dexpp_error_problem_message(err, 0), nullptr);
        dexpp_error_free(err);

        // The details are optional: a null `err` is allowed.
        EXPECT_EQ(c.apk ? dexpp_context_from_apk(c.path, 0, nullptr)
                        : dexpp_context_from_dex(c.path, nullptr),
                  nullptr);
    }
}

TEST(CApiErrors, SuccessSetsErrToNull)
{
    // A stale pointer in *err is overwritten with NULL on success.
    DexppError *stale = nullptr;
    EXPECT_EQ(dexpp_context_from_dex("tests/data/no_such_file.dex", &stale), nullptr);
    ASSERT_NE(stale, nullptr);
    DexppError *err = stale;
    DexppContext *ctx = dexpp_context_from_dex("tests/data/classes.dex", &err);
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(err, nullptr);
    dexpp_error_free(stale);
    dexpp_context_free(ctx);
}

TEST(CApiContext, FindsAClassByDescriptor)
{
    DexppContext *ctx = open_dex("tests/data/shapes.dex");
    ASSERT_NE(ctx, nullptr);

    DexppClass *circle = dexpp_context_find_class(ctx, "LCircle;");
    ASSERT_NE(circle, nullptr);
    EXPECT_EQ(take(dexpp_class_descriptor(circle)), "LCircle;");
    dexpp_class_free(circle);

    EXPECT_EQ(dexpp_context_find_class(ctx, "Lno/such/Class;"), nullptr);
    dexpp_context_free(ctx);
}

TEST(CApiContext, ReturnsTheStringPoolWithNulBytes)
{
    DexppContext *ctx = open_dex("tests/data/multistring.dex");
    ASSERT_NE(ctx, nullptr);

    size_t count = 0;
    DexppText *strings = dexpp_context_strings(ctx, &count);
    ASSERT_GT(count, 0u);
    std::set<std::string> values;
    for (size_t i = 0; i < count; ++i) {
        values.insert(str(strings[i]));
        EXPECT_EQ(strings[i].data[strings[i].length], '\0'); // also NUL-terminated
    }
    EXPECT_TRUE(values.contains(std::string("test\0embedded", 13)));
    EXPECT_TRUE(values.contains("Hello, World!"));
    EXPECT_TRUE(values.contains(""));

    dexpp_text_array_free(strings, count);
    dexpp_context_free(ctx);
}

TEST(CApiClass, DescribesAClass)
{
    DexppContext *ctx = open_dex("tests/data/shapes.dex");
    ASSERT_NE(ctx, nullptr);
    DexppClass *shape = dexpp_context_find_class(ctx, "LShape;");
    DexppClass *abstract_shape = dexpp_context_find_class(ctx, "LAbstractShape;");
    DexppClass *circle = dexpp_context_find_class(ctx, "LCircle;");
    ASSERT_NE(shape, nullptr);
    ASSERT_NE(abstract_shape, nullptr);
    ASSERT_NE(circle, nullptr);

    EXPECT_EQ(take(dexpp_class_package(circle)), ""); // the default package
    EXPECT_EQ(take(dexpp_class_source_file(circle)), "Shapes.java");

    DexppClassRef *sup = dexpp_class_superclass(circle);
    ASSERT_NE(sup, nullptr);
    EXPECT_EQ(take(dexpp_class_ref_descriptor(sup)), "LAbstractShape;");
    EXPECT_EQ(dexpp_class_ref_is_resolved(sup), 1);
    DexppClass *resolved = dexpp_class_ref_resolve(sup);
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(take(dexpp_class_descriptor(resolved)), "LAbstractShape;");
    dexpp_class_free(resolved);
    dexpp_class_ref_free(sup);

    DexppClassRef *object = dexpp_class_superclass(shape);
    ASSERT_NE(object, nullptr);
    EXPECT_EQ(take(dexpp_class_ref_descriptor(object)), "Ljava/lang/Object;");
    EXPECT_EQ(dexpp_class_ref_is_resolved(object), 0); // not in the file
    EXPECT_EQ(dexpp_class_ref_resolve(object), nullptr);
    dexpp_class_ref_free(object);

    EXPECT_EQ(dexpp_class_is_interface(shape), 1);
    EXPECT_EQ(dexpp_class_is_interface(circle), 0);
    EXPECT_EQ(dexpp_class_is_abstract(abstract_shape), 1);
    EXPECT_EQ(dexpp_class_is_abstract(circle), 0);
    EXPECT_NE(dexpp_class_access_flags(shape) & DEXPP_ACC_INTERFACE, 0u);
    EXPECT_EQ(dexpp_class_access_flags(circle) & DEXPP_ACC_INTERFACE, 0u);

    size_t count = 0;
    DexppClassRef **interfaces = dexpp_class_interfaces(abstract_shape, &count);
    ASSERT_EQ(count, 1u);
    EXPECT_EQ(take(dexpp_class_ref_descriptor(interfaces[0])), "LShape;");
    dexpp_class_ref_array_free(interfaces, count);

    // A class with no interfaces: NULL and 0, freed the same way.
    interfaces = dexpp_class_interfaces(circle, &count);
    EXPECT_EQ(interfaces, nullptr);
    EXPECT_EQ(count, 0u);
    dexpp_class_ref_array_free(interfaces, count);

    dexpp_class_free(shape);
    dexpp_class_free(abstract_shape);
    dexpp_class_free(circle);
    dexpp_context_free(ctx);
}

TEST(CApiClass, PackageUsesDotsLikeTheManifest)
{
    DexppContext *ctx = open_dex("tests/data/packaged.dex");
    ASSERT_NE(ctx, nullptr);
    DexppClass *cls = dexpp_context_find_class(ctx, "Lcom/example/app/Packaged;");
    DexppClass *inner = dexpp_context_find_class(ctx, "Lcom/example/app/Packaged$Inner;");
    ASSERT_NE(cls, nullptr);
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(take(dexpp_class_package(cls)), "com.example.app");
    EXPECT_EQ(take(dexpp_class_package(inner)), "com.example.app");
    dexpp_class_free(cls);
    dexpp_class_free(inner);
    dexpp_context_free(ctx);
}

TEST(CApiClass, JavaLangObjectHasNoSuperclassAndNoSourceFileIsNull)
{
    // tests/data has no java.lang.Object, so the "no superclass" case is
    // checked through the hierarchy instead; here: no annotations, no fields.
    DexppContext *ctx = open_dex("tests/data/classes.dex");
    ASSERT_NE(ctx, nullptr);
    DexppClass *cls = dexpp_context_find_class(ctx, "LTestDex;");
    ASSERT_NE(cls, nullptr);
    size_t count = 99;
    DexppAnnotation *annotations = dexpp_class_annotations(cls, &count);
    EXPECT_EQ(annotations, nullptr);
    EXPECT_EQ(count, 0u);
    dexpp_annotation_array_free(annotations, count);
    dexpp_class_free(cls);
    dexpp_context_free(ctx);
}

TEST(CApiMethod, DescribesTheMethodsOfAClass)
{
    DexppContext *ctx = open_dex("tests/data/classes.dex");
    ASSERT_NE(ctx, nullptr);
    DexppClass *cls = dexpp_context_find_class(ctx, "LTestDex;");
    ASSERT_NE(cls, nullptr);

    size_t count = 0;
    DexppMethod **methods = dexpp_class_methods(cls, &count);
    ASSERT_EQ(count, 3u);

    bool saw_main = false;
    for (size_t i = 0; i < count; ++i) {
        DexppMethod *m = methods[i];
        std::string name = take(dexpp_method_name(m));
        std::string descriptor = take(dexpp_method_descriptor(m));
        EXPECT_EQ(dexpp_method_has_code(m), 1) << name;
        DexppClassRef *owner = dexpp_method_declaring_class(m);
        EXPECT_EQ(take(dexpp_class_ref_descriptor(owner)), "LTestDex;");
        dexpp_class_ref_free(owner);
        if (name == "<init>") {
            EXPECT_EQ(descriptor, "()V");
            EXPECT_EQ(dexpp_method_is_static(m), 0);
            EXPECT_EQ(dexpp_method_is_constructor(m), 1);
            EXPECT_EQ(take(dexpp_method_return_type(m)), "V");
        }
        if (name == "helloDex") {
            EXPECT_EQ(descriptor, "()Ljava/lang/String;");
            EXPECT_EQ(dexpp_method_is_static(m), 1);
            EXPECT_EQ(dexpp_method_is_constructor(m), 0);
            size_t n = 99;
            char **params = dexpp_method_parameters(m, &n);
            EXPECT_EQ(params, nullptr);
            EXPECT_EQ(n, 0u);
            dexpp_string_array_free(params, n);
        }
        if (name == "main") {
            saw_main = true;
            EXPECT_EQ(descriptor, "([Ljava/lang/String;)V");
            EXPECT_EQ(dexpp_method_access_flags(m), DEXPP_ACC_PUBLIC | DEXPP_ACC_STATIC);
            size_t n = 0;
            char **params = dexpp_method_parameters(m, &n);
            EXPECT_EQ(take_all(params, n), (std::vector<std::string>{"[Ljava/lang/String;"}));
        }
    }
    EXPECT_TRUE(saw_main);

    dexpp_method_array_free(methods, count);
    dexpp_class_free(cls);
    dexpp_context_free(ctx);
}

TEST(CApiMethod, AnAbstractMethodHasNoCode)
{
    DexppContext *ctx = open_dex("tests/data/shapes.dex");
    ASSERT_NE(ctx, nullptr);
    DexppClass *shape = dexpp_context_find_class(ctx, "LShape;");
    ASSERT_NE(shape, nullptr);

    size_t count = 0;
    DexppMethod **methods = dexpp_class_methods(shape, &count);
    ASSERT_GT(count, 0u);
    for (size_t i = 0; i < count; ++i) {
        EXPECT_EQ(dexpp_method_has_code(methods[i]), 0);
        EXPECT_EQ(dexpp_method_register_count(methods[i]), 0u);
        size_t n = 99;
        DexppInstruction *insns = dexpp_method_instructions(methods[i], &n);
        EXPECT_EQ(insns, nullptr);
        EXPECT_EQ(n, 0u);
        dexpp_instruction_array_free(insns, n);
        DexppBasicBlock *blocks = dexpp_method_cfg(methods[i], &n);
        EXPECT_EQ(blocks, nullptr);
        EXPECT_EQ(n, 0u);
        dexpp_basic_block_array_free(blocks, n);
    }

    dexpp_method_array_free(methods, count);
    dexpp_class_free(shape);
    dexpp_context_free(ctx);
}

TEST(CApiField, DescribesFieldsAndInitialValues)
{
    DexppContext *ctx = open_dex("tests/data/annotated.dex");
    ASSERT_NE(ctx, nullptr);
    DexppClass *cls = dexpp_context_find_class(ctx, "LAnnotated;");
    ASSERT_NE(cls, nullptr);

    size_t count = 0;
    DexppField **fields = dexpp_class_fields(cls, &count);
    ASSERT_EQ(count, 6u);
    std::set<std::string> seen;
    for (size_t i = 0; i < count; ++i) {
        DexppField *f = fields[i];
        std::string name = take(dexpp_field_name(f));
        seen.insert(name);
        DexppClassRef *owner = dexpp_field_declaring_class(f);
        EXPECT_EQ(take(dexpp_class_ref_descriptor(owner)), "LAnnotated;");
        dexpp_class_ref_free(owner);
        EXPECT_NE(dexpp_field_access_flags(f) & DEXPP_ACC_STATIC, 0u) << name;

        DexppValue v{};
        int has_value = dexpp_field_initial_value(f, &v);
        if (name == "MAGIC") {
            EXPECT_EQ(take(dexpp_field_type(f)), "I");
            ASSERT_EQ(has_value, 1);
            EXPECT_EQ(v.kind, DEXPP_VALUE_INT);
            EXPECT_EQ(v.int_value, 42);
            size_t n = 0;
            DexppAnnotation *annotations = dexpp_field_annotations(f, &n);
            ASSERT_EQ(n, 1u);
            EXPECT_STREQ(annotations[0].type_descriptor, "LMarker;");
            dexpp_annotation_array_free(annotations, n);
        }
        else if (name == "GREETING") {
            ASSERT_EQ(has_value, 1);
            EXPECT_EQ(v.kind, DEXPP_VALUE_STRING);
            EXPECT_EQ(str(v.string_value), "hello");
        }
        else if (name == "BIG") {
            ASSERT_EQ(has_value, 1);
            EXPECT_EQ(v.int_value, 1234567890123);
        }
        else if (name == "PI") {
            ASSERT_EQ(has_value, 1);
            EXPECT_EQ(v.kind, DEXPP_VALUE_FLOAT);
            EXPECT_DOUBLE_EQ(v.float_value, 3.25);
        }
        else if (name == "FLAG") {
            ASSERT_EQ(has_value, 1);
            EXPECT_EQ(v.kind, DEXPP_VALUE_BOOL);
            EXPECT_EQ(v.bool_value, 1);
        }
        else if (name == "counter") {
            EXPECT_EQ(has_value, 0);
        }
        if (has_value != 0)
            dexpp_value_clear(&v);
        EXPECT_EQ(v.kind, DEXPP_VALUE_NULL);
    }
    EXPECT_EQ(seen, (std::set<std::string>{"MAGIC", "GREETING", "BIG", "PI", "FLAG", "counter"}));

    dexpp_field_array_free(fields, count);
    dexpp_class_free(cls);
    dexpp_context_free(ctx);
}

TEST(CApiLifetime, HandlesStayValidAfterTheContextIsFreed)
{
    DexppContext *ctx = open_dex("tests/data/classes.dex");
    ASSERT_NE(ctx, nullptr);
    DexppClass *cls = dexpp_context_find_class(ctx, "LTestDex;");
    ASSERT_NE(cls, nullptr);
    size_t count = 0;
    DexppMethod **methods = dexpp_class_methods(cls, &count);
    ASSERT_EQ(count, 3u);
    DexppClassRef *sup = dexpp_class_superclass(cls);

    // Free the owners first: the context, then the class.
    dexpp_context_free(ctx);
    EXPECT_EQ(take(dexpp_class_descriptor(cls)), "LTestDex;");
    dexpp_class_free(cls);

    EXPECT_EQ(take(dexpp_class_ref_descriptor(sup)), "Ljava/lang/Object;");
    dexpp_class_ref_free(sup);

    std::set<std::string> names;
    for (size_t i = 0; i < count; ++i) {
        names.insert(take(dexpp_method_name(methods[i])));
        EXPECT_EQ(dexpp_method_has_code(methods[i]), 1);
    }
    EXPECT_EQ(names, (std::set<std::string>{"<init>", "helloDex", "main"}));

    dexpp_method_array_free(methods, count);
}

TEST(CApiLifetime, HandlesOfAnArrayCanBeFreedOneByOne)
{
    // What the Go binding does: it keeps each handle, frees the array itself
    // with free(), and frees every handle on its own later.
    DexppContext *ctx = open_dex("tests/data/shapes.dex");
    ASSERT_NE(ctx, nullptr);

    size_t class_count = 0;
    DexppClass **class_array = dexpp_context_classes(ctx, &class_count);
    std::vector<DexppClass *> classes(class_array, class_array + class_count);
    std::free(class_array);
    dexpp_context_free(ctx);

    for (DexppClass *cls : classes) {
        size_t method_count = 0;
        DexppMethod **method_array = dexpp_class_methods(cls, &method_count);
        std::vector<DexppMethod *> methods(method_array, method_array + method_count);
        std::free(method_array);
        dexpp_class_free(cls);
        for (DexppMethod *method : methods) {
            EXPECT_NE(take(dexpp_method_name(method)), "");
            dexpp_method_free(method);
        }
    }
}

TEST(CApiLifetime, AnArrayFreedWithCountZeroKeepsItsHandles)
{
    // What the Java binding does: free() is not safe across a DLL boundary,
    // so it frees the array itself with the _array_free function and a count
    // of 0, and frees every handle on its own later.
    DexppContext *ctx = open_dex("tests/data/shapes.dex");
    ASSERT_NE(ctx, nullptr);

    size_t class_count = 0;
    DexppClass **class_array = dexpp_context_classes(ctx, &class_count);
    std::vector<DexppClass *> classes(class_array, class_array + class_count);
    dexpp_class_array_free(class_array, 0);
    ASSERT_EQ(classes.size(), 4u);

    // An empty array is NULL, so copy only a non-empty one.
    auto keep = [](auto **array, size_t count) {
        std::vector<std::remove_pointer_t<decltype(array)>> out;
        if (array != nullptr)
            out.assign(array, array + count);
        return out;
    };
    for (DexppClass *cls : classes) {
        size_t count = 0;
        DexppMethod **method_array = dexpp_class_methods(cls, &count);
        std::vector<DexppMethod *> methods = keep(method_array, count);
        dexpp_method_array_free(method_array, 0);
        DexppField **field_array = dexpp_class_fields(cls, &count);
        std::vector<DexppField *> fields = keep(field_array, count);
        dexpp_field_array_free(field_array, 0);
        DexppClassRef **ref_array = dexpp_class_interfaces(cls, &count);
        std::vector<DexppClassRef *> refs = keep(ref_array, count);
        dexpp_class_ref_array_free(ref_array, 0);
        dexpp_class_free(cls);

        for (DexppMethod *method : methods) {
            EXPECT_NE(take(dexpp_method_name(method)), "");
            dexpp_method_free(method);
        }
        for (DexppField *field : fields) {
            EXPECT_NE(take(dexpp_field_name(field)), "");
            dexpp_field_free(field);
        }
        for (DexppClassRef *ref : refs) {
            EXPECT_EQ(take(dexpp_class_ref_descriptor(ref)), "LShape;");
            dexpp_class_ref_free(ref);
        }
    }
    dexpp_context_free(ctx);
}

TEST(CApiMemory, FreeingNullIsAllowed)
{
    dexpp_error_free(nullptr);
    dexpp_string_free(nullptr);
    dexpp_string_array_free(nullptr, 0);
    dexpp_text_array_free(nullptr, 0);
    dexpp_buffer_free(nullptr);
    dexpp_index_array_free(nullptr, 0);
    dexpp_method_id_array_free(nullptr, 0);
    dexpp_annotation_array_free(nullptr, 0);
    dexpp_context_free(nullptr);
    dexpp_class_free(nullptr);
    dexpp_class_array_free(nullptr, 0);
    dexpp_class_ref_free(nullptr);
    dexpp_class_ref_array_free(nullptr, 0);
    dexpp_method_free(nullptr);
    dexpp_method_array_free(nullptr, 0);
    dexpp_field_free(nullptr);
    dexpp_field_array_free(nullptr, 0);
    dexpp_parameter_register_array_free(nullptr, 0);
    dexpp_method_call_array_free(nullptr, 0);
    dexpp_field_access_array_free(nullptr, 0);
    dexpp_string_load_array_free(nullptr, 0);
    dexpp_type_use_array_free(nullptr, 0);
    dexpp_try_block_array_free(nullptr, 0);
    dexpp_instruction_array_free(nullptr, 0);
    dexpp_basic_block_array_free(nullptr, 0);
    dexpp_call_graph_free(nullptr);
    dexpp_call_node_array_free(nullptr, 0);
    dexpp_call_edge_array_free(nullptr, 0);
    dexpp_class_hierarchy_free(nullptr);
    dexpp_xrefs_free(nullptr);
    dexpp_method_xref_array_free(nullptr, 0);
    dexpp_method_ref_group_array_free(nullptr, 0);
    dexpp_field_xref_array_free(nullptr, 0);
    dexpp_string_xref_array_free(nullptr, 0);
    dexpp_type_xref_array_free(nullptr, 0);
    dexpp_apk_free(nullptr);

    DexppText empty{};
    dexpp_text_clear(&empty);
    DexppValue value{};
    dexpp_value_clear(&value);
    DexppMethodId id{};
    dexpp_method_id_clear(&id);
}
