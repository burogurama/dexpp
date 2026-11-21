// The C API for APK metadata (bindings/c/dexpp.h): archive entries, the
// manifest, resources and signing.  See c_test_util.hpp.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../raw/test_utils.hpp"
#include "c_test_util.hpp"
#include "dexpp.h"

using c_test::list;
using c_test::message;
using c_test::str;
using c_test::take;
using c_test::take_all;

namespace {

DexppApk *open_apk(const char *path)
{
    DexppError *err = nullptr;
    DexppApk *apk = dexpp_apk_open(path, &err);
    EXPECT_NE(apk, nullptr) << message(err);
    EXPECT_EQ(err, nullptr);
    dexpp_error_free(err);
    return apk;
}

/** The descriptor of every class of @p ctx, in order. */
std::vector<std::string> class_descriptors(const DexppContext *ctx)
{
    size_t count = 0;
    DexppClass **classes = dexpp_context_classes(ctx, &count);
    std::vector<std::string> out;
    for (size_t i = 0; i < count; ++i)
        out.push_back(c_test::take(dexpp_class_descriptor(classes[i])));
    dexpp_class_array_free(classes, count);
    return out;
}

/** The bytes of entry @p name of @p apk, or an empty vector. */
std::vector<uint8_t> entry_bytes(const DexppApk *apk, const char *name)
{
    uint8_t *data = nullptr;
    size_t length = 0;
    if (dexpp_apk_read(apk, name, &data, &length) == 0)
        return {};
    std::vector<uint8_t> out(data, data + length);
    dexpp_buffer_free(data);
    return out;
}

} // namespace

TEST(CApiApk, OpenBufferMatchesOpen)
{
    // The bytes live in a malloc'd block that is freed right after the call:
    // the APK handle must have its own copy (ASan reports a use after free
    // otherwise).
    const std::vector<uint8_t> file = load_buf("tests/data/signed.apk");
    auto *bytes = static_cast<uint8_t *>(std::malloc(file.size()));
    ASSERT_NE(bytes, nullptr);
    std::memcpy(bytes, file.data(), file.size());
    DexppError *err = nullptr;
    DexppApk *apk = dexpp_apk_open_buffer(bytes, file.size(), "signed.apk", &err);
    std::free(bytes);
    ASSERT_NE(apk, nullptr) << message(err);
    EXPECT_EQ(err, nullptr);

    DexppApk *from_file = open_apk("tests/data/signed.apk");
    ASSERT_NE(from_file, nullptr);
    EXPECT_EQ(list([&](size_t *n) { return dexpp_apk_entries(apk, n); }),
              list([&](size_t *n) { return dexpp_apk_entries(from_file, n); }));
    EXPECT_EQ(entry_bytes(apk, "classes.dex"), entry_bytes(from_file, "classes.dex"));
    EXPECT_FALSE(entry_bytes(apk, "classes.dex").empty());
    EXPECT_EQ(c_test::take(dexpp_apk_package(apk)), "com.example.dexpp");
    DexppText text{};
    ASSERT_EQ(dexpp_apk_resolve_string(apk, 0x7f010000, &text), 1);
    EXPECT_EQ(str(text), "Dexpp Sample");
    dexpp_text_clear(&text);

    DexppSigningInfo signing{};
    DexppSigningInfo signing_from_file{};
    dexpp_apk_signing(apk, &signing);
    dexpp_apk_signing(from_file, &signing_from_file);
    EXPECT_EQ(signing.v1, 1);
    EXPECT_EQ(signing.v2, 1);
    EXPECT_EQ(signing.v3, 1);
    ASSERT_EQ(signing.certificate_count, 1u);
    ASSERT_EQ(signing_from_file.certificate_count, 1u);
    EXPECT_STREQ(signing.certificates[0].sha256_hex, signing_from_file.certificates[0].sha256_hex);
    dexpp_signing_info_clear(&signing);
    dexpp_signing_info_clear(&signing_from_file);

    DexppContext *ctx = dexpp_apk_analysis(apk, 1, &err);
    ASSERT_NE(ctx, nullptr) << message(err);
    DexppClass *cls = dexpp_context_find_class(ctx, "LTestDex;");
    EXPECT_NE(cls, nullptr);
    dexpp_class_free(cls);
    dexpp_context_free(ctx);
    dexpp_apk_free(from_file);
    dexpp_apk_free(apk);
}

TEST(CApiApk, OpenBufferErrors)
{
    const char not_an_apk[] = "not an apk";
    const auto *text = reinterpret_cast<const uint8_t *>(not_an_apk);
    struct Case
    {
        const uint8_t *bytes;
        size_t len;
        const char *label;
        const char *prefix;
    };
    for (const Case &c : {Case{text, sizeof not_an_apk - 1, nullptr, "<apk>: "},
                          Case{text, sizeof not_an_apk - 1, "upload.apk", "upload.apk: "},
                          Case{text, 0, nullptr, "<apk>: "}, Case{nullptr, 0, nullptr, "<apk>: "},
                          Case{nullptr, 5, "null.apk", "null.apk: "}}) {
        SCOPED_TRACE(std::string(c.prefix) + std::to_string(c.len));
        DexppError *err = nullptr;
        EXPECT_EQ(dexpp_apk_open_buffer(c.bytes, c.len, c.label, &err), nullptr);
        ASSERT_NE(err, nullptr);
        EXPECT_EQ(dexpp_error_code(err), DEXPP_ERROR_INVALID_APK);
        EXPECT_EQ(message(err).rfind(c.prefix, 0), 0u) << message(err);
        dexpp_error_free(err);
        EXPECT_EQ(dexpp_apk_open_buffer(c.bytes, c.len, c.label, nullptr), nullptr);
    }

    // The label also starts the errors of the analysis.
    const std::vector<uint8_t> nodex = load_buf("tests/data/nodex.apk");
    DexppError *err = nullptr;
    DexppApk *apk = dexpp_apk_open_buffer(nodex.data(), nodex.size(), "nodex-upload.apk", &err);
    ASSERT_NE(apk, nullptr) << message(err);
    EXPECT_EQ(dexpp_apk_analysis(apk, 1, &err), nullptr);
    ASSERT_NE(err, nullptr);
    EXPECT_EQ(dexpp_error_code(err), DEXPP_ERROR_INVALID_APK);
    EXPECT_EQ(message(err).rfind("nodex-upload.apk: ", 0), 0u) << message(err);
    dexpp_error_free(err);
    dexpp_apk_free(apk);
}

TEST(CApiApk, AnalysisWithAnyThreadCount)
{
    // multidex.apk has four deflated DEX entries, enough for the parallel path.
    DexppApk *apk = open_apk("tests/data/multidex.apk");
    ASSERT_NE(apk, nullptr);
    std::vector<std::string> expected;
    for (uint32_t threads : {1u, 0u, 4u, 1u, 0u}) {
        SCOPED_TRACE(threads);
        DexppError *err = nullptr;
        DexppContext *ctx = dexpp_apk_analysis(apk, threads, &err);
        ASSERT_NE(ctx, nullptr) << message(err);
        EXPECT_EQ(err, nullptr);
        std::vector<std::string> classes = class_descriptors(ctx);
        dexpp_context_free(ctx);
        if (expected.empty()) {
            expected = classes;
            ASSERT_FALSE(expected.empty());
            EXPECT_EQ(expected.front(), "LTestDex;");
        }
        EXPECT_EQ(classes, expected);
    }
    dexpp_apk_free(apk);
}

TEST(CApiApk, ReadsTheWholeManifest)
{
    DexppApk *apk = open_apk("tests/data/sample.apk");
    ASSERT_NE(apk, nullptr);
    EXPECT_EQ(take(dexpp_apk_package(apk)), "com.example.dexpp");

    DexppManifest m{};
    ASSERT_EQ(dexpp_apk_manifest(apk, &m), 1);
    EXPECT_STREQ(m.package, "com.example.dexpp");
    EXPECT_EQ(m.has_version_code, 1);
    EXPECT_EQ(m.version_code, 7);
    EXPECT_STREQ(m.version_name, "1.2.3");
    EXPECT_EQ(m.min_sdk, 21);
    EXPECT_EQ(m.target_sdk, 34);
    ASSERT_EQ(m.permission_count, 2u);
    EXPECT_STREQ(m.permissions[0], "android.permission.INTERNET");
    ASSERT_EQ(m.declared_permission_count, 1u);
    EXPECT_STREQ(m.declared_permissions[0].name, "com.example.dexpp.CUSTOM");
    EXPECT_EQ(m.debuggable, 1);
    EXPECT_EQ(m.allow_backup, 0);
    EXPECT_STREQ(m.launcher_activity, "com.example.dexpp.MainActivity");

    ASSERT_EQ(m.component_count, 4u);
    std::map<std::string, const DexppComponent *> by_name;
    for (size_t i = 0; i < m.component_count; ++i)
        by_name[m.components[i].name] = &m.components[i];
    const DexppComponent *main = by_name["com.example.dexpp.MainActivity"];
    ASSERT_NE(main, nullptr);
    EXPECT_EQ(main->kind, DEXPP_COMPONENT_ACTIVITY);
    EXPECT_EQ(main->exported, 1);
    EXPECT_EQ(main->is_exported, 1);
    EXPECT_EQ(main->is_reachable, 1);
    ASSERT_EQ(main->intent_filter_count, 1u);
    EXPECT_EQ(main->intent_filters[0].is_launcher, 1);
    ASSERT_EQ(main->intent_filters[0].action_count, 1u);
    EXPECT_STREQ(main->intent_filters[0].actions[0], "android.intent.action.MAIN");
    const DexppComponent *sync = by_name["com.example.dexpp.SyncService"];
    ASSERT_NE(sync, nullptr);
    EXPECT_EQ(sync->kind, DEXPP_COMPONENT_SERVICE);
    EXPECT_EQ(sync->is_exported, 0);
    EXPECT_EQ(by_name["com.example.dexpp.BootReceiver"]->kind, DEXPP_COMPONENT_RECEIVER);
    EXPECT_EQ(by_name["com.example.dexpp.DataProvider"]->kind, DEXPP_COMPONENT_PROVIDER);

    dexpp_manifest_clear(&m);
    EXPECT_EQ(m.package, nullptr);
    dexpp_apk_free(apk);
}

TEST(CApiApk, SeparatesExportedFromReachable)
{
    DexppApk *apk = open_apk("tests/data/reachable.apk");
    ASSERT_NE(apk, nullptr);

    DexppManifest m{};
    ASSERT_EQ(dexpp_apk_manifest(apk, &m), 1);
    std::map<std::string, int> levels;
    for (size_t i = 0; i < m.declared_permission_count; ++i)
        if (m.declared_permissions[i].has_protection_level == 1)
            levels[m.declared_permissions[i].name] = m.declared_permissions[i].protection_level;
    EXPECT_EQ(levels["com.dexpp.reach.P_NORMAL"], 0);
    EXPECT_EQ(levels["com.dexpp.reach.P_DANGEROUS"], 1);
    EXPECT_EQ(levels["com.dexpp.reach.P_SIGNATURE"], 2);

    ASSERT_EQ(m.component_count, 18u);
    size_t exported = 0;
    size_t reachable = 0;
    for (size_t i = 0; i < m.component_count; ++i) {
        const DexppComponent &c = m.components[i];
        exported += static_cast<size_t>(c.is_exported);
        reachable += static_cast<size_t>(c.is_reachable);
        if (std::string(c.name) == "com.dexpp.reach.SigActivity") {
            EXPECT_EQ(c.is_exported, 1);
            EXPECT_EQ(c.is_reachable, 0);
            EXPECT_EQ(c.guarded_by_signature_permission, 1);
            EXPECT_STREQ(c.permission, "com.dexpp.reach.P_SIGNATURE");
            EXPECT_EQ(c.permission_inherited, 0);
        }
        // No permission of its own: it takes the application's.
        if (std::string(c.name) == "com.dexpp.reach.InheritingActivity") {
            EXPECT_STREQ(c.permission, "com.dexpp.reach.P_SIGNATURE");
            EXPECT_EQ(c.permission_inherited, 1);
            EXPECT_EQ(c.permission_unknown, 0);
            EXPECT_EQ(c.read_permission, nullptr);
            EXPECT_EQ(c.is_reachable, 0);
        }
        // A provider: a normal read permission, the application's for writing.
        if (std::string(c.name) == "com.dexpp.reach.ReadableProvider") {
            EXPECT_STREQ(c.read_permission, "com.dexpp.reach.P_NORMAL");
            EXPECT_STREQ(c.write_permission, "com.dexpp.reach.P_SIGNATURE");
            EXPECT_EQ(c.read_permission_reference, 0u);
            EXPECT_EQ(c.read_permission_unknown, 0);
            EXPECT_EQ(c.write_permission_reference, 0u);
            EXPECT_EQ(c.write_permission_unknown, 0);
            EXPECT_EQ(c.is_reachable, 1);
        }
    }
    EXPECT_EQ(exported, 16u);
    EXPECT_EQ(reachable, 8u);
    ASSERT_NE(m.application_permission, nullptr);
    EXPECT_STREQ(m.application_permission, "com.dexpp.reach.P_SIGNATURE");
    EXPECT_EQ(m.application_permission_reference, 0u);
    EXPECT_EQ(m.application_permission_unknown, 0);
    dexpp_manifest_clear(&m);
    EXPECT_EQ(m.application_permission, nullptr);

    // This APK has a manifest but no code.
    DexppError *err = nullptr;
    EXPECT_EQ(dexpp_apk_analysis(apk, 0, &err), nullptr);
    ASSERT_NE(err, nullptr);
    EXPECT_EQ(dexpp_error_code(err), DEXPP_ERROR_INVALID_APK);
    dexpp_error_free(err);
    EXPECT_EQ(dexpp_apk_analysis(apk, 0, nullptr), nullptr);
    dexpp_apk_free(apk);
}

TEST(CApiApk, AnApkWithoutAManifestOrResources)
{
    DexppApk *apk = open_apk("tests/data/app.apk");
    ASSERT_NE(apk, nullptr);
    EXPECT_EQ(dexpp_apk_package(apk), nullptr);
    DexppManifest m{};
    EXPECT_EQ(dexpp_apk_manifest(apk, &m), 0);
    EXPECT_EQ(m.package, nullptr);
    EXPECT_EQ(dexpp_apk_has_resources(apk), 0);
    EXPECT_EQ(dexpp_apk_resources_empty(apk), 1);
    DexppText t{};
    EXPECT_EQ(dexpp_apk_resolve_string(apk, 0x7f010000, &t), 0);
    uint32_t id = 0;
    EXPECT_EQ(dexpp_apk_resource_id(apk, "string", "app_name", &id), 0);
    DexppResourceName name{};
    EXPECT_EQ(dexpp_apk_resource_name(apk, 0x7f010000, &name), 0);

    DexppContext *ctx = dexpp_apk_analysis(apk, 0, nullptr);
    ASSERT_NE(ctx, nullptr);
    dexpp_apk_free(apk); // the context does not need the APK handle
    DexppClass *cls = dexpp_context_find_class(ctx, "LFibonacci;");
    EXPECT_NE(cls, nullptr);
    dexpp_class_free(cls);
    dexpp_context_free(ctx);
}

TEST(CApiApk, ResolvesResources)
{
    DexppApk *apk = open_apk("tests/data/sample.apk");
    ASSERT_NE(apk, nullptr);
    EXPECT_EQ(dexpp_apk_has_resources(apk), 1);
    EXPECT_EQ(dexpp_apk_resources_empty(apk), 0);

    uint32_t id = 0;
    ASSERT_EQ(dexpp_apk_resource_id(apk, "string", "app_name", &id), 1);
    EXPECT_EQ(id >> 24, 0x7fu);
    DexppResourceName name{};
    ASSERT_EQ(dexpp_apk_resource_name(apk, id, &name), 1);
    EXPECT_STREQ(name.package, "com.example.dexpp");
    EXPECT_STREQ(name.type, "string");
    EXPECT_STREQ(name.entry, "app_name");
    dexpp_resource_name_clear(&name);
    DexppText value{};
    ASSERT_EQ(dexpp_apk_resolve_string(apk, id, &value), 1);
    EXPECT_EQ(str(value), "Dexpp Sample");
    dexpp_text_clear(&value);

    EXPECT_EQ(dexpp_apk_resource_id(apk, "string", "nope", &id), 0);
    EXPECT_EQ(dexpp_apk_resolve_string(apk, 0x7f999999, &value), 0);
    dexpp_apk_free(apk);
}

TEST(CApiApk, ResolvesReferencesInTheManifest)
{
    // refs.apk writes its boolean and integer attributes as resource
    // references (tests/data/RefsManifest.xml).
    DexppApk *apk = open_apk("tests/data/refs.apk");
    ASSERT_NE(apk, nullptr);
    uint32_t yes = 0, no = 0, min_sdk = 0, debug = 0, not_a_bool = 0;
    ASSERT_EQ(dexpp_apk_resource_id(apk, "bool", "yes", &yes), 1);
    ASSERT_EQ(dexpp_apk_resource_id(apk, "bool", "no", &no), 1);
    ASSERT_EQ(dexpp_apk_resource_id(apk, "integer", "min_sdk", &min_sdk), 1);
    ASSERT_EQ(dexpp_apk_resource_id(apk, "bool", "debug", &debug), 1);
    ASSERT_EQ(dexpp_apk_resource_id(apk, "string", "not_a_bool", &not_a_bool), 1);

    DexppManifest m{};
    ASSERT_EQ(dexpp_apk_manifest(apk, &m), 1);
    EXPECT_EQ(m.has_min_sdk, 1);
    EXPECT_EQ(m.min_sdk, 23);
    EXPECT_EQ(m.min_sdk_reference, min_sdk);
    EXPECT_EQ(m.has_target_sdk, 1);
    EXPECT_EQ(m.target_sdk, 34);
    EXPECT_NE(m.target_sdk_reference, 0u);
    EXPECT_EQ(m.has_version_code, 1);
    EXPECT_EQ(m.version_code, 42);
    EXPECT_EQ(m.version_code_reference, 0u); // a plain value
    EXPECT_EQ(m.debuggable, 1);
    EXPECT_EQ(m.debuggable_reference, debug);
    EXPECT_EQ(m.debuggable_varies, 1); // false in values-v31
    EXPECT_EQ(m.allow_backup, 0);
    EXPECT_NE(m.allow_backup_reference, 0u);
    EXPECT_EQ(m.allow_backup_varies, 1); // true in values-v31
    ASSERT_NE(m.version_name, nullptr);
    EXPECT_STREQ(m.version_name, "1.0");
    EXPECT_NE(m.version_name_reference, 0u);

    std::map<std::string, const DexppComponent *> by_name;
    for (size_t i = 0; i < m.component_count; ++i)
        by_name[m.components[i].name] = &m.components[i];
    const DexppComponent *exported = by_name["com.dexpp.refs.ExportedActivity"];
    ASSERT_NE(exported, nullptr);
    EXPECT_EQ(exported->exported, 1);
    EXPECT_EQ(exported->exported_reference, yes);
    EXPECT_EQ(exported->is_exported, 1);
    const DexppComponent *hidden = by_name["com.dexpp.refs.HiddenActivity"];
    ASSERT_NE(hidden, nullptr);
    EXPECT_EQ(hidden->exported, 0);
    EXPECT_EQ(hidden->exported_reference, no);
    EXPECT_EQ(hidden->exported_varies, 0);
    EXPECT_EQ(hidden->is_exported, 0);
    EXPECT_EQ(hidden->name_reference, 0u);
    // false by default, true in values-v31: possibly exported.
    const DexppComponent *variant = by_name["com.dexpp.refs.VariantActivity"];
    ASSERT_NE(variant, nullptr);
    EXPECT_EQ(variant->exported, 0);
    EXPECT_EQ(variant->exported_varies, 1);
    EXPECT_EQ(variant->is_exported, 1);
    // A reference to the string "true": Android reads it as true.
    const DexppComponent *text = by_name["com.dexpp.refs.TextActivity"];
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->exported, 1);
    EXPECT_EQ(text->exported_reference, not_a_bool);
    EXPECT_EQ(text->is_exported, 1);
    EXPECT_EQ(text->is_reachable, 1);

    std::map<std::string, const DexppDeclaredPermission *> declared;
    for (size_t i = 0; i < m.declared_permission_count; ++i)
        declared[m.declared_permissions[i].name] = &m.declared_permissions[i];
    ASSERT_EQ(declared.size(), 4u);
    EXPECT_EQ(declared["com.dexpp.refs.P_SIGNATURE"]->has_protection_level, 1);
    EXPECT_EQ(declared["com.dexpp.refs.P_SIGNATURE"]->protection_level, 2);
    EXPECT_EQ(declared["com.dexpp.refs.P_SIGNATURE"]->protection_level_varies, 0);
    EXPECT_EQ(declared["com.dexpp.refs.P_TEXT"]->has_protection_level, 0);
    EXPECT_EQ(declared["com.dexpp.refs.P_VARYING"]->protection_level, 2);
    EXPECT_EQ(declared["com.dexpp.refs.P_VARYING"]->protection_level_varies, 1);
    EXPECT_EQ(by_name["com.dexpp.refs.GuardedService"]->is_reachable, 0);
    EXPECT_EQ(by_name["com.dexpp.refs.TextGuardedService"]->is_reachable, 1);
    EXPECT_EQ(by_name["com.dexpp.refs.VaryingLevelService"]->is_reachable, 1);

    // android:permission given by a reference.
    const DexppComponent *by_reference = by_name["com.dexpp.refs.PermissionByReferenceService"];
    ASSERT_NE(by_reference, nullptr);
    ASSERT_NE(by_reference->permission, nullptr);
    EXPECT_STREQ(by_reference->permission, "com.dexpp.refs.P_SIGNATURE");
    EXPECT_NE(by_reference->permission_reference, 0u);
    EXPECT_EQ(by_reference->is_reachable, 0);
    // A string with a res/values-fr variant: Android ignores it.
    const DexppComponent *localized = by_name["com.dexpp.refs.LocalizedPermissionService"];
    ASSERT_NE(localized, nullptr);
    EXPECT_EQ(localized->permission, nullptr);
    EXPECT_NE(localized->permission_reference, 0u);
    EXPECT_EQ(localized->permission_unknown, 0);
    EXPECT_EQ(localized->is_reachable, 1);
    // A string that differs between Android versions: unknown.
    EXPECT_EQ(by_name["com.dexpp.refs.PermissionOnV31Service"]->permission_unknown, 1);
    // A theme attribute: unknown, so possibly exported.
    const DexppComponent *theme = by_name["com.dexpp.refs.ThemeActivity"];
    ASSERT_NE(theme, nullptr);
    EXPECT_EQ(theme->exported, -1);
    EXPECT_EQ(theme->exported_unknown, 1);
    EXPECT_EQ(theme->is_exported, 1);
    EXPECT_EQ(m.version_code_unknown, 0);
    EXPECT_EQ(m.version_name_unknown, 0);
    EXPECT_EQ(m.min_sdk_unknown, 0);
    EXPECT_EQ(m.target_sdk_unknown, 0);
    EXPECT_EQ(m.debuggable_unknown, 0);
    EXPECT_EQ(m.allow_backup_unknown, 0);
    dexpp_manifest_clear(&m);

    // The plain-value manifest of sample.apk has no references.
    DexppApk *sample = open_apk("tests/data/sample.apk");
    ASSERT_NE(sample, nullptr);
    ASSERT_EQ(dexpp_apk_manifest(sample, &m), 1);
    EXPECT_EQ(m.min_sdk_reference, 0u);
    EXPECT_EQ(m.debuggable_reference, 0u);
    EXPECT_EQ(m.version_name_reference, 0u);
    EXPECT_EQ(m.debuggable_varies, 0);
    EXPECT_EQ(m.allow_backup_varies, 0);
    EXPECT_EQ(m.application_permission, nullptr);
    for (size_t i = 0; i < m.component_count; ++i) {
        EXPECT_EQ(m.components[i].exported_reference, 0u) << m.components[i].name;
        EXPECT_EQ(m.components[i].permission_reference, 0u) << m.components[i].name;
        EXPECT_EQ(m.components[i].exported_varies, 0) << m.components[i].name;
        EXPECT_EQ(m.components[i].exported_unknown, 0) << m.components[i].name;
        EXPECT_EQ(m.components[i].name_unknown, 0) << m.components[i].name;
        EXPECT_EQ(m.components[i].permission_inherited, 0) << m.components[i].name;
    }
    dexpp_manifest_clear(&m);
    dexpp_apk_free(sample);
    dexpp_apk_free(apk);
}

TEST(CApiApk, NullValuesReadAsNotWritten)
{
    // refs.apk: @bool/nothing is @null, @bool/same_as_nothing refers to it,
    // and @string/no_permission is @null.
    DexppApk *apk = open_apk("tests/data/refs.apk");
    ASSERT_NE(apk, nullptr);
    uint32_t nothing = 0, no_permission = 0;
    ASSERT_EQ(dexpp_apk_resource_id(apk, "bool", "nothing", &nothing), 1);
    ASSERT_EQ(dexpp_apk_resource_id(apk, "string", "no_permission", &no_permission), 1);

    DexppManifest m{};
    ASSERT_EQ(dexpp_apk_manifest(apk, &m), 1);
    std::map<std::string, const DexppComponent *> by_name;
    for (size_t i = 0; i < m.component_count; ++i)
        by_name[m.components[i].name] = &m.components[i];

    const DexppComponent *null = by_name["com.dexpp.refs.NullActivity"];
    ASSERT_NE(null, nullptr);
    EXPECT_EQ(null->exported, -1);
    EXPECT_EQ(null->exported_reference, nothing);
    EXPECT_EQ(null->exported_unknown, 0);
    EXPECT_EQ(null->is_exported, 0); // no intent filter
    EXPECT_EQ(by_name["com.dexpp.refs.NullFilteredActivity"]->is_exported, 1);
    EXPECT_EQ(by_name["com.dexpp.refs.LiteralNullActivity"]->exported_reference, 0u);

    const DexppComponent *service = by_name["com.dexpp.refs.NullPermissionService"];
    ASSERT_NE(service, nullptr);
    EXPECT_EQ(service->permission, nullptr);
    EXPECT_EQ(service->permission_reference, no_permission);
    EXPECT_EQ(service->permission_unknown, 0);
    EXPECT_EQ(service->permission_inherited, 0);
    EXPECT_EQ(service->is_reachable, 1);
    dexpp_manifest_clear(&m);
    dexpp_apk_free(apk);
}

TEST(CApiApk, ListsTheValuesOfEveryConfiguration)
{
    DexppApk *apk = open_apk("tests/data/refs.apk");
    ASSERT_NE(apk, nullptr);
    uint32_t differs = 0, greeting = 0, same_as_yes = 0, yes = 0;
    ASSERT_EQ(dexpp_apk_resource_id(apk, "bool", "differs_on_v31", &differs), 1);
    ASSERT_EQ(dexpp_apk_resource_id(apk, "string", "greeting", &greeting), 1);
    ASSERT_EQ(dexpp_apk_resource_id(apk, "bool", "same_as_yes", &same_as_yes), 1);
    ASSERT_EQ(dexpp_apk_resource_id(apk, "bool", "yes", &yes), 1);

    size_t count = 99;
    DexppResourceValue *values = dexpp_apk_resource_values(apk, differs, &count);
    ASSERT_EQ(count, 2u);
    EXPECT_STREQ(values[0].configuration, "");
    EXPECT_EQ(values[0].type, 0x12);
    EXPECT_EQ(values[0].data, 0u);
    EXPECT_EQ(values[0].string.data, nullptr);
    EXPECT_EQ(values[0].boolean, 0);
    EXPECT_STREQ(values[1].configuration, "v31");
    EXPECT_EQ(values[1].boolean, 1);
    EXPECT_EQ(values[1].has_integer, 1);
    EXPECT_EQ(values[1].integer, -1);
    dexpp_resource_value_array_free(values, count);

    values = dexpp_apk_resource_values(apk, greeting, &count);
    ASSERT_EQ(count, 3u);
    EXPECT_STREQ(values[1].configuration, "fr");
    ASSERT_NE(values[1].string.data, nullptr);
    EXPECT_EQ(std::string(values[1].string.data, values[1].string.length), "Bonjour");
    EXPECT_STREQ(values[2].configuration, "b+sr+Latn-land-night-v8");
    EXPECT_EQ(values[2].has_integer, 0);
    EXPECT_EQ(values[2].boolean, 0); // "Zdravo" is not "true"
    dexpp_resource_value_array_free(values, count);

    // A reference is not followed.
    values = dexpp_apk_resource_values(apk, same_as_yes, &count);
    ASSERT_EQ(count, 1u);
    EXPECT_EQ(values[0].type, 0x01);
    EXPECT_EQ(values[0].data, yes);
    EXPECT_EQ(values[0].boolean, -1);
    EXPECT_EQ(values[0].has_integer, 0);
    dexpp_resource_value_array_free(values, count);

    count = 99;
    EXPECT_EQ(dexpp_apk_resource_values(apk, 0x7f999999, &count), nullptr);
    EXPECT_EQ(count, 0u);
    dexpp_resource_value_array_free(nullptr, 0);
    dexpp_apk_free(apk);

    // No resource table at all.
    DexppApk *bare = open_apk("tests/data/app.apk");
    ASSERT_NE(bare, nullptr);
    count = 99;
    EXPECT_EQ(dexpp_apk_resource_values(bare, differs, &count), nullptr);
    EXPECT_EQ(count, 0u);
    dexpp_apk_free(bare);
}

TEST(CApiApk, ResolvesBooleanAndIntegerResources)
{
    DexppApk *apk = open_apk("tests/data/refs.apk");
    ASSERT_NE(apk, nullptr);
    uint32_t yes = 0, same_as_yes = 0, no = 0, min_sdk = 0, not_a_bool = 0;
    ASSERT_EQ(dexpp_apk_resource_id(apk, "bool", "yes", &yes), 1);
    ASSERT_EQ(dexpp_apk_resource_id(apk, "bool", "same_as_yes", &same_as_yes), 1);
    ASSERT_EQ(dexpp_apk_resource_id(apk, "bool", "no", &no), 1);
    ASSERT_EQ(dexpp_apk_resource_id(apk, "integer", "min_sdk", &min_sdk), 1);
    ASSERT_EQ(dexpp_apk_resource_id(apk, "string", "not_a_bool", &not_a_bool), 1);

    int b = -1;
    ASSERT_EQ(dexpp_apk_resolve_bool(apk, yes, &b), 1);
    EXPECT_EQ(b, 1);
    ASSERT_EQ(dexpp_apk_resolve_bool(apk, same_as_yes, &b), 1);
    EXPECT_EQ(b, 1);
    ASSERT_EQ(dexpp_apk_resolve_bool(apk, no, &b), 1);
    EXPECT_EQ(b, 0);
    // The string "true" reads as true, as Android reads it.
    ASSERT_EQ(dexpp_apk_resolve_bool(apk, not_a_bool, &b), 1);
    EXPECT_EQ(b, 1);
    EXPECT_EQ(dexpp_apk_resolve_bool(apk, 0x7f999999, &b), 0);

    int64_t n = 0;
    ASSERT_EQ(dexpp_apk_resolve_int(apk, min_sdk, &n), 1);
    EXPECT_EQ(n, 23);
    EXPECT_EQ(dexpp_apk_resolve_int(apk, not_a_bool, &n), 0);
    EXPECT_EQ(dexpp_apk_resolve_int(apk, 0x7f999999, &n), 0);
    dexpp_apk_free(apk);

    // No resource table at all.
    DexppApk *bare = open_apk("tests/data/app.apk");
    ASSERT_NE(bare, nullptr);
    EXPECT_EQ(dexpp_apk_resolve_bool(bare, yes, &b), 0);
    EXPECT_EQ(dexpp_apk_resolve_int(bare, min_sdk, &n), 0);
    dexpp_apk_free(bare);
}

TEST(CApiApk, ListsAndReadsArchiveEntries)
{
    DexppApk *apk = open_apk("tests/data/sample.apk");
    ASSERT_NE(apk, nullptr);
    EXPECT_EQ(c_test::list([&](size_t *c) { return dexpp_apk_entries(apk, c); }),
              (std::vector<std::string>{"AndroidManifest.xml", "resources.arsc", "classes.dex"}));

    uint8_t *bytes = nullptr;
    size_t length = 0;
    ASSERT_EQ(dexpp_apk_read(apk, "classes.dex", &bytes, &length), 1);
    ASSERT_GE(length, 8u);
    EXPECT_EQ(bytes[0], 'd');
    EXPECT_EQ(bytes[1], 'e');
    EXPECT_EQ(bytes[2], 'x');
    dexpp_buffer_free(bytes);
    EXPECT_EQ(dexpp_apk_read(apk, "nope", &bytes, &length), 0);
    EXPECT_EQ(bytes, nullptr);
    EXPECT_EQ(length, 0u);
    dexpp_apk_free(apk);
}

TEST(CApiApk, ReadsSigningInformation)
{
    DexppApk *apk = open_apk("tests/data/signed.apk");
    ASSERT_NE(apk, nullptr);
    DexppSigningInfo info{};
    dexpp_apk_signing(apk, &info);
    EXPECT_EQ(info.v1, 1);
    EXPECT_EQ(info.v2, 1);
    EXPECT_EQ(info.v3, 1);
    ASSERT_EQ(info.certificate_count, 1u);
    const DexppCertificate &cert = info.certificates[0];
    EXPECT_STREQ(cert.subject, "C=MA, O=DexppOrg, CN=Dexpp Test");
    EXPECT_STREQ(cert.issuer, cert.subject);
    EXPECT_EQ(cert.self_signed, 1);
    EXPECT_STREQ(cert.serial_hex, "ba51e610667560a3");
    EXPECT_STREQ(cert.sha256_hex,
                 "6c9647205d1ae141f9104587153cb1c71c5da9251335da1a1977de500c6ef599");
    EXPECT_EQ(std::string(cert.sha1_hex).size(), 40u);
    EXPECT_GT(cert.der_length, 100u);
    EXPECT_EQ(cert.der[0], 0x30); // a DER SEQUENCE
    EXPECT_EQ(std::string(cert.not_before).size(), 20u);
    EXPECT_EQ(std::string(cert.not_after).size(), 20u);
    dexpp_signing_info_clear(&info);
    EXPECT_EQ(info.certificates, nullptr);
    dexpp_apk_free(apk);

    apk = open_apk("tests/data/sample.apk");
    ASSERT_NE(apk, nullptr);
    dexpp_apk_signing(apk, &info);
    EXPECT_EQ(info.v1 + info.v2 + info.v3, 0);
    EXPECT_EQ(info.certificate_count, 0u);
    EXPECT_EQ(info.certificates, nullptr);
    dexpp_signing_info_clear(&info);
    dexpp_apk_free(apk);
}

TEST(CApiApk, OpenErrorsCarryACode)
{
    struct Case
    {
        const char *path;
        DexppErrorCode code;
    };
    for (const Case &c : {Case{"tests/data/no_such_file.apk", DEXPP_ERROR_FILE_NOT_FOUND},
                          Case{"tests/data/classes.dex", DEXPP_ERROR_INVALID_APK},
                          Case{"tests/data", DEXPP_ERROR_CANNOT_READ}}) {
        SCOPED_TRACE(c.path);
        DexppError *err = nullptr;
        EXPECT_EQ(dexpp_apk_open(c.path, &err), nullptr);
        ASSERT_NE(err, nullptr);
        EXPECT_EQ(dexpp_error_code(err), c.code);
        EXPECT_NE(message(err), "");
        dexpp_error_free(err);
        EXPECT_EQ(dexpp_apk_open(c.path, nullptr), nullptr);
    }
}
