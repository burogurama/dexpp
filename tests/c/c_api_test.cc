// The C API for analysis (bindings/c/dexpp.h), called the way a C program
// calls it.  Every allocation is freed with the function the header names for
// it, so the sanitizer run reports a leak, a double free or a use after free
// in the shim.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "dexpp.h"

namespace {

/** Copy a string the C API returned and free it. */
std::string take(char *s)
{
    EXPECT_NE(s, nullptr);
    std::string copy = s == nullptr ? "" : s;
    dexpp_string_free(s);
    return copy;
}

DexppContext *open_dex(const char *path)
{
    char *err = nullptr;
    DexppContext *ctx = dexpp_context_from_dex(path, &err);
    EXPECT_NE(ctx, nullptr) << (err == nullptr ? "" : err);
    EXPECT_EQ(err, nullptr);
    dexpp_string_free(err);
    return ctx;
}

} // namespace

TEST(CApiContext, LoadsADexFileAndListsItsClasses)
{
    DexppContext *ctx = open_dex("tests/data/shapes.dex");
    ASSERT_NE(ctx, nullptr);

    size_t count = 0;
    DexppClass **classes = dexpp_context_classes(ctx, &count);
    ASSERT_EQ(count, 4u);
    std::set<std::string> names;
    for (size_t i = 0; i < count; ++i)
        names.insert(take(dexpp_class_name(classes[i])));
    EXPECT_EQ(names,
              (std::set<std::string>{"LShape;", "LAbstractShape;", "LCircle;", "LRectangle;"}));

    dexpp_class_array_free(classes, count);
    dexpp_context_free(ctx);
}

TEST(CApiContext, LoadsAnApkInMultidexOrder)
{
    char *err = nullptr;
    DexppContext *ctx = dexpp_context_from_apk("tests/data/app.apk", &err);
    ASSERT_NE(ctx, nullptr) << (err == nullptr ? "" : err);
    EXPECT_EQ(err, nullptr);

    size_t count = 0;
    DexppClass **classes = dexpp_context_classes(ctx, &count);
    ASSERT_EQ(count, 2u);
    EXPECT_EQ(take(dexpp_class_name(classes[0])), "LTestDex;");
    EXPECT_EQ(take(dexpp_class_name(classes[1])), "LFibonacci;");

    dexpp_class_array_free(classes, count);
    dexpp_context_free(ctx);
}

TEST(CApiContext, ReportsLoadErrorsThroughErr)
{
    struct Case
    {
        const char *path;
        bool apk;
    };
    for (const Case &c : {Case{"tests/data/no_such_file.dex", false},
                          Case{"tests/data/TestDex.java", false}, // not a DEX file
                          Case{"tests/data", false},              // a directory
                          Case{"tests/data/no_such_file.apk", true},
                          Case{"tests/data/classes.dex", true}, // not a ZIP archive
                          Case{"tests/data/nodex.apk", true}}) {
        SCOPED_TRACE(c.path);
        char *err = nullptr;
        DexppContext *ctx =
            c.apk ? dexpp_context_from_apk(c.path, &err) : dexpp_context_from_dex(c.path, &err);
        EXPECT_EQ(ctx, nullptr);
        ASSERT_NE(err, nullptr);
        EXPECT_NE(std::string(err), "");
        dexpp_string_free(err);

        // The message is optional: a null `err` is allowed.
        EXPECT_EQ(c.apk ? dexpp_context_from_apk(c.path, nullptr)
                        : dexpp_context_from_dex(c.path, nullptr),
                  nullptr);
    }
}

TEST(CApiContext, FindsAClassByDescriptor)
{
    DexppContext *ctx = open_dex("tests/data/shapes.dex");
    ASSERT_NE(ctx, nullptr);

    DexppClass *circle = dexpp_context_find_class(ctx, "LCircle;");
    ASSERT_NE(circle, nullptr);
    EXPECT_EQ(take(dexpp_class_name(circle)), "LCircle;");
    dexpp_class_free(circle);

    EXPECT_EQ(dexpp_context_find_class(ctx, "Lno/such/Class;"), nullptr);
    dexpp_context_free(ctx);
}

TEST(CApiContext, ReturnsTheStringPool)
{
    DexppContext *ctx = open_dex("tests/data/classes.dex");
    ASSERT_NE(ctx, nullptr);

    size_t count = 0;
    char **strings = dexpp_context_strings(ctx, &count);
    ASSERT_GT(count, 0u);
    std::set<std::string> values;
    for (size_t i = 0; i < count; ++i)
        values.insert(strings[i]);
    EXPECT_TRUE(values.contains("Hello Dex++!!"));
    EXPECT_TRUE(values.contains("LTestDex;"));

    dexpp_string_array_free(strings, count);
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
    EXPECT_EQ(take(dexpp_class_superclass(circle)), "LAbstractShape;");
    EXPECT_EQ(take(dexpp_class_superclass(shape)), "Ljava/lang/Object;");

    EXPECT_EQ(dexpp_class_is_interface(shape), 1);
    EXPECT_EQ(dexpp_class_is_interface(circle), 0);
    EXPECT_EQ(dexpp_class_is_abstract(abstract_shape), 1);
    EXPECT_EQ(dexpp_class_is_abstract(circle), 0);
    const unsigned int kInterface = 0x0200;
    EXPECT_NE(dexpp_class_access_flags(shape) & kInterface, 0u);
    EXPECT_EQ(dexpp_class_access_flags(circle) & kInterface, 0u);

    size_t count = 0;
    char **interfaces = dexpp_class_interfaces(abstract_shape, &count);
    ASSERT_EQ(count, 1u);
    EXPECT_STREQ(interfaces[0], "LShape;");
    dexpp_string_array_free(interfaces, count);

    // A class with no interfaces: the result is still freed the same way.
    interfaces = dexpp_class_interfaces(circle, &count);
    EXPECT_EQ(count, 0u);
    dexpp_string_array_free(interfaces, count);

    dexpp_class_free(shape);
    dexpp_class_free(abstract_shape);
    dexpp_class_free(circle);
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
        std::string name = take(dexpp_method_name(methods[i]));
        std::string descriptor = take(dexpp_method_descriptor(methods[i]));
        EXPECT_EQ(dexpp_method_has_code(methods[i]), 1) << name;
        if (name == "<init>") {
            EXPECT_EQ(descriptor, "()V");
            EXPECT_EQ(dexpp_method_is_static(methods[i]), 0);
            EXPECT_EQ(dexpp_method_call_count(methods[i]), 1u);
        }
        if (name == "helloDex") {
            EXPECT_EQ(descriptor, "()Ljava/lang/String;");
            EXPECT_EQ(dexpp_method_is_static(methods[i]), 1);
            EXPECT_EQ(dexpp_method_call_count(methods[i]), 0u);
        }
        if (name == "main") {
            saw_main = true;
            EXPECT_EQ(descriptor, "([Ljava/lang/String;)V");
            EXPECT_EQ(dexpp_method_is_static(methods[i]), 1);
            EXPECT_EQ(dexpp_method_access_flags(methods[i]), 0x9u); // public static
            EXPECT_EQ(dexpp_method_call_count(methods[i]), 2u);
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
        EXPECT_EQ(dexpp_method_call_count(methods[i]), 0u);
    }

    dexpp_method_array_free(methods, count);
    dexpp_class_free(shape);
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

    // Free the owners first: the context, then the class.
    dexpp_context_free(ctx);
    EXPECT_EQ(take(dexpp_class_name(cls)), "LTestDex;");
    dexpp_class_free(cls);

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

TEST(CApiApk, ReadsThePackageAndItsComponents)
{
    char *err = nullptr;
    DexppApk *apk = dexpp_apk_open("tests/data/sample.apk", &err);
    ASSERT_NE(apk, nullptr) << (err == nullptr ? "" : err);
    EXPECT_EQ(err, nullptr);

    EXPECT_EQ(take(dexpp_apk_package(apk)), "com.example.dexpp");

    size_t count = 0;
    DexppComponent *components = dexpp_apk_components(apk, &count);
    ASSERT_EQ(count, 4u);
    for (size_t i = 0; i < count; ++i) {
        std::string name = components[i].name;
        if (name == "com.example.dexpp.MainActivity") {
            EXPECT_EQ(components[i].kind, 0);
            EXPECT_EQ(components[i].exported, 1);
            EXPECT_EQ(components[i].reachable, 1);
        }
        if (name == "com.example.dexpp.SyncService") {
            EXPECT_EQ(components[i].kind, 1);
            EXPECT_EQ(components[i].exported, 0);
            EXPECT_EQ(components[i].reachable, 0);
        }
        if (name == "com.example.dexpp.BootReceiver") {
            EXPECT_EQ(components[i].kind, 2);
        }
        if (name == "com.example.dexpp.DataProvider") {
            EXPECT_EQ(components[i].kind, 3);
        }
    }
    dexpp_component_array_free(components, count);

    DexppContext *ctx = dexpp_apk_analysis(apk, &err);
    ASSERT_NE(ctx, nullptr) << (err == nullptr ? "" : err);
    EXPECT_EQ(err, nullptr);
    // The context does not need the APK handle.
    dexpp_apk_free(apk);
    DexppClass *cls = dexpp_context_find_class(ctx, "LTestDex;");
    EXPECT_NE(cls, nullptr);
    dexpp_class_free(cls);
    dexpp_context_free(ctx);
}

TEST(CApiApk, SeparatesExportedFromReachable)
{
    DexppApk *apk = dexpp_apk_open("tests/data/reachable.apk", nullptr);
    ASSERT_NE(apk, nullptr);

    size_t count = 0;
    DexppComponent *components = dexpp_apk_components(apk, &count);
    ASSERT_EQ(count, 8u);
    size_t exported = 0;
    size_t reachable = 0;
    for (size_t i = 0; i < count; ++i) {
        exported += static_cast<size_t>(components[i].exported);
        reachable += static_cast<size_t>(components[i].reachable);
        if (std::string(components[i].name) == "com.dexpp.reach.SigActivity") {
            EXPECT_EQ(components[i].exported, 1);
            EXPECT_EQ(components[i].reachable, 0);
        }
    }
    EXPECT_EQ(exported, 7u);
    EXPECT_EQ(reachable, 5u);
    dexpp_component_array_free(components, count);

    // This APK has a manifest but no code.
    char *err = nullptr;
    EXPECT_EQ(dexpp_apk_analysis(apk, &err), nullptr);
    ASSERT_NE(err, nullptr);
    dexpp_string_free(err);
    EXPECT_EQ(dexpp_apk_analysis(apk, nullptr), nullptr);

    dexpp_apk_free(apk);
}

TEST(CApiApk, AnApkWithoutAManifestHasNoPackageAndNoComponents)
{
    DexppApk *apk = dexpp_apk_open("tests/data/app.apk", nullptr);
    ASSERT_NE(apk, nullptr);

    EXPECT_EQ(dexpp_apk_package(apk), nullptr);
    size_t count = 99;
    DexppComponent *components = dexpp_apk_components(apk, &count);
    EXPECT_EQ(components, nullptr);
    EXPECT_EQ(count, 0u);
    dexpp_component_array_free(components, count);

    dexpp_apk_free(apk);
}

TEST(CApiApk, ReportsOpenErrorsThroughErr)
{
    for (const char *path :
         {"tests/data/no_such_file.apk", "tests/data/classes.dex", "tests/data"}) {
        SCOPED_TRACE(path);
        char *err = nullptr;
        EXPECT_EQ(dexpp_apk_open(path, &err), nullptr);
        ASSERT_NE(err, nullptr);
        EXPECT_NE(std::string(err), "");
        dexpp_string_free(err);
        EXPECT_EQ(dexpp_apk_open(path, nullptr), nullptr);
    }
}

TEST(CApiStrings, FreeingNullIsAllowed)
{
    dexpp_string_free(nullptr);
    dexpp_string_array_free(nullptr, 0);
    dexpp_context_free(nullptr);
    dexpp_class_free(nullptr);
    dexpp_method_free(nullptr);
    dexpp_apk_free(nullptr);
}
