// Tests for the APK layer: Apk (archive + manifest + analysis bridge) and the
// AXML / Manifest decoders, against tests/data/sample.apk (a real aapt2-built
// APK; SampleManifest.xml alongside is its source).

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "dex.hpp"

using namespace dex;
using namespace dex::apk;

namespace {

Apk open_sample()
{
    auto apk = Apk::open("tests/data/sample.apk");
    EXPECT_TRUE(apk.has_value()) << (apk ? "" : apk.error().message);
    return std::move(*apk);
}

const Component *find_component(const Manifest &m, std::string_view name)
{
    for (const auto &c : m.components())
        if (c.name == name)
            return &c;
    return nullptr;
}

} // namespace

TEST(ApkTest, OpenAndEntries)
{
    auto apk = open_sample();
    auto entries = apk.entries();
    EXPECT_NE(std::find(entries.begin(), entries.end(), "AndroidManifest.xml"), entries.end());
    EXPECT_NE(std::find(entries.begin(), entries.end(), "classes.dex"), entries.end());

    EXPECT_TRUE(apk.read("classes.dex").has_value());
    EXPECT_FALSE(apk.read("does/not/exist").has_value());
}

TEST(ApkTest, OpenErrors)
{
    EXPECT_EQ(Apk::open("tests/data/missing.apk").error().code, AnalysisError::Code::FileNotFound);
    EXPECT_EQ(Apk::open("tests/data/classes.dex").error().code, AnalysisError::Code::InvalidApk);
}

TEST(ApkTest, AnalysisBridge)
{
    auto apk = open_sample();
    auto ctx = apk.analysis();
    ASSERT_TRUE(ctx.has_value()) << ctx.error().message;
    EXPECT_TRUE(ctx->find_class("LTestDex;").has_value());

    // analysis() is cached: a second call returns an equivalent context.
    auto ctx2 = apk.analysis();
    ASSERT_TRUE(ctx2.has_value());
    EXPECT_EQ(ctx->classes().size(), ctx2->classes().size());
}

TEST(ManifestTest, PackageAndVersions)
{
    auto apk = open_sample();
    const Manifest *m = apk.manifest();
    ASSERT_NE(m, nullptr);

    EXPECT_EQ(m->package(), "com.example.dexpp");
    EXPECT_EQ(m->version_code().value_or(-1), 7);
    ASSERT_TRUE(m->version_name().has_value());
    EXPECT_EQ(*m->version_name(), "1.2.3");
    EXPECT_EQ(m->min_sdk().value_or(-1), 21);
    EXPECT_EQ(m->target_sdk().value_or(-1), 34);
}

TEST(ManifestTest, Permissions)
{
    auto apk = open_sample();
    const Manifest *m = apk.manifest();
    ASSERT_NE(m, nullptr);

    const auto &perms = m->permissions();
    EXPECT_NE(std::find(perms.begin(), perms.end(), "android.permission.INTERNET"), perms.end());
    EXPECT_NE(std::find(perms.begin(), perms.end(), "android.permission.CAMERA"), perms.end());

    ASSERT_EQ(m->declared_permissions().size(), 1u);
    EXPECT_EQ(m->declared_permissions()[0], "com.example.dexpp.CUSTOM");
}

TEST(ManifestTest, ComponentsAndExportedFlags)
{
    auto apk = open_sample();
    const Manifest *m = apk.manifest();
    ASSERT_NE(m, nullptr);

    EXPECT_EQ(m->components_of(Component::Kind::Activity).size(), 1u);
    EXPECT_EQ(m->components_of(Component::Kind::Service).size(), 1u);
    EXPECT_EQ(m->components_of(Component::Kind::Receiver).size(), 1u);
    EXPECT_EQ(m->components_of(Component::Kind::Provider).size(), 1u);

    // Names are qualified against the package (leaf ".Name" -> fully-qualified).
    const Component *activity = find_component(*m, "com.example.dexpp.MainActivity");
    ASSERT_NE(activity, nullptr);
    EXPECT_TRUE(activity->is_exported());

    const Component *service = find_component(*m, "com.example.dexpp.SyncService");
    ASSERT_NE(service, nullptr);
    EXPECT_FALSE(service->is_exported());

    const Component *receiver = find_component(*m, "com.example.dexpp.BootReceiver");
    ASSERT_NE(receiver, nullptr);
    ASSERT_TRUE(receiver->permission.has_value());
    EXPECT_EQ(*receiver->permission, "android.permission.BIND_DEVICE_ADMIN");
}

TEST(ManifestTest, IntentFiltersAndLauncher)
{
    auto apk = open_sample();
    const Manifest *m = apk.manifest();
    ASSERT_NE(m, nullptr);

    const Component *activity = find_component(*m, "com.example.dexpp.MainActivity");
    ASSERT_NE(activity, nullptr);
    ASSERT_EQ(activity->intent_filters.size(), 1u);
    EXPECT_TRUE(activity->intent_filters[0].is_launcher());
    EXPECT_TRUE(activity->intent_filters[0].has_action("android.intent.action.MAIN"));

    ASSERT_TRUE(m->launcher_activity().has_value());
    EXPECT_EQ(*m->launcher_activity(), "com.example.dexpp.MainActivity");
}

TEST(ManifestTest, ApplicationFlags)
{
    auto apk = open_sample();
    const Manifest *m = apk.manifest();
    ASSERT_NE(m, nullptr);

    EXPECT_EQ(m->debuggable().value_or(false), true);
    EXPECT_EQ(m->allow_backup().value_or(true), false);
}

// tests/data/reachable.apk (source ReachableManifest.xml) declares permissions at
// every protection level and guards exported components with them, exercising
// Component::is_reachable and the declared-permission protectionLevel lookup.
TEST(ManifestTest, DeclaredPermissionProtectionLevels)
{
    auto apk = Apk::open("tests/data/reachable.apk");
    ASSERT_TRUE(apk.has_value()) << (apk ? "" : apk.error().message);
    const Manifest *m = apk->manifest();
    ASSERT_NE(m, nullptr);

    EXPECT_EQ(m->declared_permission_protection_level("com.dexpp.reach.P_NORMAL"), 0);
    EXPECT_EQ(m->declared_permission_protection_level("com.dexpp.reach.P_DANGEROUS"), 1);
    EXPECT_EQ(m->declared_permission_protection_level("com.dexpp.reach.P_SIGNATURE"), 2);
    // Permissions the app does not declare (e.g. platform permissions) are unknown.
    EXPECT_FALSE(m->declared_permission_protection_level("android.permission.DUMP").has_value());
}

TEST(ManifestTest, Reachability)
{
    auto apk = Apk::open("tests/data/reachable.apk");
    ASSERT_TRUE(apk.has_value()) << (apk ? "" : apk.error().message);
    const Manifest *m = apk->manifest();
    ASSERT_NE(m, nullptr);

    auto reachable = [&](std::string_view name) {
        const Component *c = find_component(*m, name);
        EXPECT_NE(c, nullptr) << name;
        return c != nullptr && c->is_reachable();
    };
    auto exported = [&](std::string_view name) {
        const Component *c = find_component(*m, name);
        return c != nullptr && c->is_exported();
    };

    // Exported and unguarded, or guarded by a normal/dangerous permission a
    // third-party app can hold -> reachable.
    EXPECT_TRUE(reachable("com.dexpp.reach.OpenActivity"));
    EXPECT_TRUE(reachable("com.dexpp.reach.NormalActivity"));
    EXPECT_TRUE(reachable("com.dexpp.reach.DangerousActivity"));
    EXPECT_TRUE(reachable("com.dexpp.reach.OpenReceiver"));
    // Guarded by an undeclared (platform) permission -> still reachable (unknown level).
    EXPECT_TRUE(reachable("com.dexpp.reach.PlatformPermActivity"));

    // Guarded by a signature permission the app declares -> not reachable,
    // even though it is exported.
    EXPECT_TRUE(exported("com.dexpp.reach.SigActivity"));
    EXPECT_FALSE(reachable("com.dexpp.reach.SigActivity"));
    EXPECT_TRUE(find_component(*m, "com.dexpp.reach.SigActivity")->guarded_by_signature_permission);
    EXPECT_TRUE(exported("com.dexpp.reach.SigService"));
    EXPECT_FALSE(reachable("com.dexpp.reach.SigService"));

    // Not exported -> not reachable.
    EXPECT_FALSE(exported("com.dexpp.reach.HiddenActivity"));
    EXPECT_FALSE(reachable("com.dexpp.reach.HiddenActivity"));

    // Counts: 5 of 6 activities exported, 4 reachable.
    int act_exported = 0, act_reachable = 0;
    for (const auto *c : m->components_of(Component::Kind::Activity)) {
        act_exported += c->is_exported();
        act_reachable += c->is_reachable();
    }
    EXPECT_EQ(act_exported, 5);
    EXPECT_EQ(act_reachable, 4);
}
