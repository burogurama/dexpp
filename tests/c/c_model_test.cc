// The C API for the editable model (bindings/c/dexpp_model.h), called the way
// a C program calls it.  Every allocation is freed with the function the
// header names for it, so the sanitizer run reports a leak, a double free or
// a use after free in the shim.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "dexpp_model.h"

namespace {

const char *const kShapes = "tests/data/shapes.dex";

DexppModelDex *read_shapes()
{
    DexppError *err = nullptr;
    DexppModelDex *model = dexpp_model_read_file(kShapes, 1, &err);
    EXPECT_NE(model, nullptr) << (err == nullptr ? "" : dexpp_error_message(err));
    EXPECT_EQ(err, nullptr);
    dexpp_error_free(err);
    return model;
}

std::vector<std::string> descriptors_of(const DexppModelDex *model)
{
    size_t count = 0;
    char **names = dexpp_model_class_descriptors(model, &count);
    std::vector<std::string> out(names, names + count);
    dexpp_string_array_free(names, count);
    return out;
}

/** Expect a failed call's error code and message, then free the error. */
void expect_error(DexppError *err, DexppErrorCode code)
{
    ASSERT_NE(err, nullptr);
    EXPECT_EQ(dexpp_error_code(err), code);
    EXPECT_NE(std::string(dexpp_error_message(err)), "");
    dexpp_error_free(err);
}

} // namespace

TEST(CModelRead, ReadsAFileAndListsItsClasses)
{
    DexppModelDex *model = read_shapes();
    ASSERT_NE(model, nullptr);

    EXPECT_EQ(dexpp_model_version(model), 35u);
    EXPECT_EQ(dexpp_model_class_count(model), 4u);
    EXPECT_EQ(descriptors_of(model),
              (std::vector<std::string>{"LShape;", "LAbstractShape;", "LCircle;", "LRectangle;"}));
    EXPECT_EQ(dexpp_model_has_class(model, "LCircle;"), 1);
    EXPECT_EQ(dexpp_model_has_class(model, "Lno/such/Class;"), 0);

    dexpp_model_free(model);
}

TEST(CModelRead, ReadsBytes)
{
    std::ifstream in(kShapes, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    ASSERT_FALSE(bytes.empty());

    for (int debug_info : {0, 1}) {
        DexppError *err = nullptr;
        DexppModelDex *model = dexpp_model_read(bytes.data(), bytes.size(), debug_info, &err);
        ASSERT_NE(model, nullptr) << (err == nullptr ? "" : dexpp_error_message(err));
        EXPECT_EQ(err, nullptr);
        EXPECT_EQ(dexpp_model_class_count(model), 4u);
        dexpp_model_free(model);
    }
}

TEST(CModelRead, ReportsReadErrorsThroughErr)
{
    const std::vector<uint8_t> junk = {'n', 'o', 't', ' ', 'a', ' ', 'd', 'e', 'x'};
    DexppError *err = nullptr;
    EXPECT_EQ(dexpp_model_read(junk.data(), junk.size(), 1, &err), nullptr);
    expect_error(err, DEXPP_ERROR_INVALID_DEX);
    EXPECT_EQ(dexpp_model_read(junk.data(), junk.size(), 1, nullptr), nullptr);

    struct Case
    {
        const char *path;
        DexppErrorCode code;
    };
    for (const Case &c : {Case{"tests/data/no_such_file.dex", DEXPP_ERROR_FILE_NOT_FOUND},
                          Case{"tests/data/TestDex.java", DEXPP_ERROR_INVALID_DEX},
                          Case{"tests/data", DEXPP_ERROR_CANNOT_READ}}) {
        SCOPED_TRACE(c.path);
        err = nullptr;
        EXPECT_EQ(dexpp_model_read_file(c.path, 1, &err), nullptr);
        expect_error(err, c.code);
        EXPECT_EQ(dexpp_model_read_file(c.path, 1, nullptr), nullptr);
    }
}

TEST(CModelLifetime, NewCloneAndFree)
{
    DexppModelDex *empty = dexpp_model_new(39);
    ASSERT_NE(empty, nullptr);
    EXPECT_EQ(dexpp_model_version(empty), 39u);
    EXPECT_EQ(dexpp_model_class_count(empty), 0u);
    dexpp_model_set_version(empty, 35);
    EXPECT_EQ(dexpp_model_version(empty), 35u);

    // An empty model has no descriptors: NULL and a count of 0.
    size_t count = 99;
    char **names = dexpp_model_class_descriptors(empty, &count);
    EXPECT_EQ(names, nullptr);
    EXPECT_EQ(count, 0u);
    dexpp_string_array_free(names, count);
    dexpp_model_free(empty);

    DexppModelDex *model = read_shapes();
    ASSERT_NE(model, nullptr);
    DexppModelDex *copy = dexpp_model_clone(model);
    ASSERT_NE(copy, nullptr);
    // The copy is independent: it survives the original and changes alone.
    EXPECT_EQ(dexpp_model_remove_class(model, "LCircle;"), 1);
    dexpp_model_free(model);
    EXPECT_EQ(dexpp_model_class_count(copy), 4u);
    EXPECT_EQ(dexpp_model_has_class(copy, "LCircle;"), 1);
    dexpp_model_free(copy);

    dexpp_model_free(nullptr);
}

TEST(CModelWrite, WritesBytesThatReadBack)
{
    DexppModelDex *model = read_shapes();
    ASSERT_NE(model, nullptr);

    DexppError *err = nullptr;
    size_t len = 0;
    uint8_t *bytes = dexpp_model_write(model, 1, &len, &err);
    ASSERT_NE(bytes, nullptr) << (err == nullptr ? "" : dexpp_error_message(err));
    EXPECT_EQ(err, nullptr);
    ASSERT_GT(len, 0u);

    DexppModelDex *again = dexpp_model_read(bytes, len, 1, &err);
    ASSERT_NE(again, nullptr) << (err == nullptr ? "" : dexpp_error_message(err));
    EXPECT_EQ(dexpp_model_class_count(again), 4u);

    // Writing is repeatable: the same bytes again.
    size_t len2 = 0;
    uint8_t *bytes2 = dexpp_model_write(again, 1, &len2, &err);
    ASSERT_NE(bytes2, nullptr);
    EXPECT_EQ(std::vector<uint8_t>(bytes, bytes + len),
              std::vector<uint8_t>(bytes2, bytes2 + len2));

    dexpp_buffer_free(bytes);
    dexpp_buffer_free(bytes2);
    dexpp_buffer_free(nullptr);
    dexpp_model_free(again);
    dexpp_model_free(model);
}

TEST(CModelWrite, WritesAFile)
{
    DexppModelDex *model = read_shapes();
    ASSERT_NE(model, nullptr);
    const std::string path =
        (std::filesystem::temp_directory_path() / "dexpp_c_model_test.dex").string();

    DexppError *err = nullptr;
    ASSERT_EQ(dexpp_model_write_file(model, path.c_str(), 1, &err), 1)
        << (err ? dexpp_error_message(err) : "");
    EXPECT_EQ(err, nullptr);
    DexppModelDex *again = dexpp_model_read_file(path.c_str(), 1, &err);
    ASSERT_NE(again, nullptr) << (err == nullptr ? "" : dexpp_error_message(err));
    EXPECT_EQ(dexpp_model_class_count(again), 4u);
    dexpp_model_free(again);
    std::filesystem::remove(path);

    // A folder that does not exist: the write fails and says why.
    const char *bad = "tests/data/no/such/folder/out.dex";
    EXPECT_EQ(dexpp_model_write_file(model, bad, 1, &err), 0);
    expect_error(err, DEXPP_ERROR_CANNOT_WRITE);
    EXPECT_EQ(dexpp_model_write_file(model, bad, 1, nullptr), 0);

    dexpp_model_free(model);
}

TEST(CModelValidate, AValidModelHasNoProblems)
{
    DexppModelDex *model = read_shapes();
    ASSERT_NE(model, nullptr);

    size_t count = 99;
    DexppProblem *problems = dexpp_model_validate(model, &count);
    EXPECT_EQ(problems, nullptr);
    EXPECT_EQ(count, 0u);
    dexpp_problem_array_free(problems, count);

    EXPECT_EQ(dexpp_model_required_version(model), 35u);
    DexppPoolStats stats = dexpp_model_pool_stats(model);
    EXPECT_GT(stats.strings, 0u);
    EXPECT_EQ(stats.types, 8u);
    EXPECT_EQ(stats.fields, 4u);
    EXPECT_EQ(stats.methods, 12u);
    EXPECT_EQ(stats.method_handles, 0u);
    EXPECT_EQ(stats.call_sites, 0u);

    dexpp_model_free(model);
}

TEST(CModelValidate, AnInvalidModelListsItsProblemsAndDoesNotWrite)
{
    DexppModelDex *model = read_shapes();
    ASSERT_NE(model, nullptr);

    // Rename a class to something that is not a class descriptor.
    const char *from[] = {"LCircle;"};
    const char *to[] = {"not a descriptor"};
    dexpp_model_rename_types(model, from, to, 1);

    size_t count = 0;
    DexppProblem *problems = dexpp_model_validate(model, &count);
    ASSERT_NE(problems, nullptr);
    ASSERT_GT(count, 0u);
    for (size_t i = 0; i < count; ++i) {
        ASSERT_NE(problems[i].where, nullptr);
        ASSERT_NE(problems[i].message, nullptr);
        EXPECT_NE(std::string(problems[i].message), "");
    }
    dexpp_problem_array_free(problems, count);

    DexppError *err = nullptr;
    size_t len = 0;
    EXPECT_EQ(dexpp_model_write(model, 1, &len, &err), nullptr);
    ASSERT_NE(err, nullptr);
    // The error lists the same problems validate() returns.
    EXPECT_EQ(dexpp_error_problem_count(err), count);
    for (size_t i = 0; i < dexpp_error_problem_count(err); ++i) {
        EXPECT_NE(dexpp_error_problem_where(err, i), nullptr);
        EXPECT_NE(std::string(dexpp_error_problem_message(err, i)), "");
    }
    expect_error(err, DEXPP_ERROR_INVALID_MODEL);
    EXPECT_EQ(dexpp_model_write(model, 1, &len, nullptr), nullptr);

    const std::string path =
        (std::filesystem::temp_directory_path() / "dexpp_c_model_invalid.dex").string();
    err = nullptr;
    EXPECT_EQ(dexpp_model_write_file(model, path.c_str(), 1, &err), 0);
    expect_error(err, DEXPP_ERROR_INVALID_MODEL);
    EXPECT_FALSE(std::filesystem::exists(path));

    dexpp_model_free(model);
}

TEST(CModelClasses, DescribesAClass)
{
    DexppModelDex *model = read_shapes();
    ASSERT_NE(model, nullptr);

    DexppClassInfo info{};
    ASSERT_EQ(dexpp_model_class_info(model, "LCircle;", &info), 1);
    EXPECT_STREQ(info.descriptor, "LCircle;");
    EXPECT_STREQ(info.superclass, "LAbstractShape;");
    EXPECT_EQ(info.interfaces, nullptr); // no interfaces: NULL and 0
    EXPECT_EQ(info.interface_count, 0u);
    EXPECT_EQ(info.field_count, 1u);
    EXPECT_EQ(info.method_count, 4u);
    dexpp_class_info_clear(&info);
    // The strings are cleared, so freeing the same struct again is harmless.
    EXPECT_EQ(info.descriptor, nullptr);
    dexpp_class_info_clear(&info);

    ASSERT_EQ(dexpp_model_class_info(model, "LAbstractShape;", &info), 1);
    ASSERT_EQ(info.interface_count, 1u);
    EXPECT_STREQ(info.interfaces[0], "LShape;");
    dexpp_class_info_clear(&info);
    EXPECT_EQ(info.interfaces, nullptr);
    EXPECT_EQ(info.interface_count, 0u);

    // An absent class leaves `out` as it was.
    DexppClassInfo untouched{};
    untouched.field_count = 7;
    EXPECT_EQ(dexpp_model_class_info(model, "Lno/such/Class;", &untouched), 0);
    EXPECT_EQ(untouched.descriptor, nullptr);
    EXPECT_EQ(untouched.interfaces, nullptr);
    EXPECT_EQ(untouched.field_count, 7u);

    dexpp_model_free(model);
}

TEST(CModelEdit, MovesCopiesAndRemovesClasses)
{
    DexppModelDex *src = read_shapes();
    ASSERT_NE(src, nullptr);
    DexppModelDex *dst = dexpp_model_new(dexpp_model_version(src));

    DexppError *err = nullptr;
    ASSERT_EQ(dexpp_model_copy_class(dst, src, "LCircle;", &err), 1)
        << (err ? dexpp_error_message(err) : "");
    EXPECT_EQ(err, nullptr);
    EXPECT_EQ(dexpp_model_has_class(src, "LCircle;"), 1); // the copy left the source alone
    EXPECT_EQ(dexpp_model_has_class(dst, "LCircle;"), 1);

    ASSERT_EQ(dexpp_model_move_class(dst, src, "LRectangle;", &err), 1)
        << (err ? dexpp_error_message(err) : "");
    EXPECT_EQ(err, nullptr);
    EXPECT_EQ(dexpp_model_has_class(src, "LRectangle;"), 0); // the move took it out
    EXPECT_EQ(descriptors_of(dst), (std::vector<std::string>{"LCircle;", "LRectangle;"}));

    // The destination already has the class.
    EXPECT_EQ(dexpp_model_copy_class(dst, src, "LCircle;", &err), 0);
    expect_error(err, DEXPP_ERROR_MODEL);
    err = nullptr;
    EXPECT_EQ(dexpp_model_move_class(dst, src, "LCircle;", &err), 0);
    expect_error(err, DEXPP_ERROR_MODEL);
    // The source has no such class.
    err = nullptr;
    EXPECT_EQ(dexpp_model_copy_class(dst, src, "Lno/such/Class;", &err), 0);
    expect_error(err, DEXPP_ERROR_MODEL);
    err = nullptr;
    EXPECT_EQ(dexpp_model_move_class(dst, src, "Lno/such/Class;", &err), 0);
    expect_error(err, DEXPP_ERROR_MODEL);
    // The message is optional.
    EXPECT_EQ(dexpp_model_copy_class(dst, src, "LCircle;", nullptr), 0);
    EXPECT_EQ(dexpp_model_move_class(dst, src, "Lno/such/Class;", nullptr), 0);
    EXPECT_EQ(dexpp_model_class_count(src), 3u); // a failed call changes nothing

    EXPECT_EQ(dexpp_model_remove_class(dst, "LCircle;"), 1);
    EXPECT_EQ(dexpp_model_remove_class(dst, "LCircle;"), 0);
    EXPECT_EQ(dexpp_model_class_count(dst), 1u);

    // The class that moved outlives the model it came from.
    dexpp_model_free(src);
    size_t len = 0;
    err = nullptr;
    uint8_t *bytes = dexpp_model_write(dst, 1, &len, &err);
    ASSERT_NE(bytes, nullptr) << (err == nullptr ? "" : dexpp_error_message(err));
    dexpp_buffer_free(bytes);
    dexpp_model_free(dst);
}

TEST(CModelEdit, RenamesClassesEverywhere)
{
    DexppModelDex *model = read_shapes();
    ASSERT_NE(model, nullptr);

    const char *from[] = {"LCircle;", "LShape;"};
    const char *to[] = {"Lcom/example/Round;", "Lcom/example/Form;"};
    dexpp_model_rename_types(model, from, to, 2);

    EXPECT_EQ(dexpp_model_has_class(model, "LCircle;"), 0);
    EXPECT_EQ(dexpp_model_has_class(model, "Lcom/example/Round;"), 1);
    // The rename reaches references too: the class that implemented LShape;.
    DexppClassInfo info{};
    ASSERT_EQ(dexpp_model_class_info(model, "LAbstractShape;", &info), 1);
    ASSERT_EQ(info.interface_count, 1u);
    EXPECT_STREQ(info.interfaces[0], "Lcom/example/Form;");
    dexpp_class_info_clear(&info);

    // No pairs: nothing changes.
    dexpp_model_rename_types(model, nullptr, nullptr, 0);
    EXPECT_EQ(dexpp_model_class_count(model), 4u);

    size_t problems = 0;
    EXPECT_EQ(dexpp_model_validate(model, &problems), nullptr);
    EXPECT_EQ(problems, 0u);
    dexpp_model_free(model);
}
