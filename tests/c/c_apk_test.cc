// The C API for APK metadata (bindings/c/dexpp.h): archive entries, the
// manifest, resources and signing.  See c_test_util.hpp.

#include <gtest/gtest.h>

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "c_test_util.hpp"
#include "dexpp.h"

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

} // namespace

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

    ASSERT_EQ(m.component_count, 8u);
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
        }
    }
    EXPECT_EQ(exported, 7u);
    EXPECT_EQ(reachable, 5u);
    dexpp_manifest_clear(&m);

    // This APK has a manifest but no code.
    DexppError *err = nullptr;
    EXPECT_EQ(dexpp_apk_analysis(apk, &err), nullptr);
    ASSERT_NE(err, nullptr);
    EXPECT_EQ(dexpp_error_code(err), DEXPP_ERROR_INVALID_APK);
    dexpp_error_free(err);
    EXPECT_EQ(dexpp_apk_analysis(apk, nullptr), nullptr);
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

    DexppContext *ctx = dexpp_apk_analysis(apk, nullptr);
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
    EXPECT_EQ(m.allow_backup, 0);
    EXPECT_NE(m.allow_backup_reference, 0u);

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
    EXPECT_EQ(hidden->is_exported, 0);
    // A reference to a string: set, but unknown, so possibly exported.
    const DexppComponent *text = by_name["com.dexpp.refs.TextActivity"];
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->exported, -1);
    EXPECT_EQ(text->exported_reference, not_a_bool);
    EXPECT_EQ(text->is_exported, 1);
    EXPECT_EQ(text->is_reachable, 1);

    std::map<std::string, const DexppDeclaredPermission *> declared;
    for (size_t i = 0; i < m.declared_permission_count; ++i)
        declared[m.declared_permissions[i].name] = &m.declared_permissions[i];
    ASSERT_EQ(declared.size(), 2u);
    EXPECT_EQ(declared["com.dexpp.refs.P_SIGNATURE"]->has_protection_level, 1);
    EXPECT_EQ(declared["com.dexpp.refs.P_SIGNATURE"]->protection_level, 2);
    EXPECT_EQ(declared["com.dexpp.refs.P_TEXT"]->has_protection_level, 0);
    EXPECT_EQ(by_name["com.dexpp.refs.GuardedService"]->is_reachable, 0);
    EXPECT_EQ(by_name["com.dexpp.refs.TextGuardedService"]->is_reachable, 1);
    dexpp_manifest_clear(&m);

    // The plain-value manifest of sample.apk has no references.
    DexppApk *sample = open_apk("tests/data/sample.apk");
    ASSERT_NE(sample, nullptr);
    ASSERT_EQ(dexpp_apk_manifest(sample, &m), 1);
    EXPECT_EQ(m.min_sdk_reference, 0u);
    EXPECT_EQ(m.debuggable_reference, 0u);
    for (size_t i = 0; i < m.component_count; ++i)
        EXPECT_EQ(m.components[i].exported_reference, 0u) << m.components[i].name;
    dexpp_manifest_clear(&m);
    dexpp_apk_free(sample);
    dexpp_apk_free(apk);
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
    EXPECT_EQ(dexpp_apk_resolve_bool(apk, not_a_bool, &b), 0);
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
