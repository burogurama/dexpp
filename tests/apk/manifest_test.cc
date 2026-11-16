// Tests for the APK layer: Apk (archive + manifest + analysis bridge) and the
// AXML / Manifest decoders, against tests/data/sample.apk (a real aapt2-built
// APK; SampleManifest.xml alongside is its source).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../raw/test_utils.hpp"
#include "arsc_builder.hpp"
#include "dex.hpp"
#include "raw/zip.hpp"

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
    EXPECT_EQ(Apk::open("tests/data/missing.apk").error().code, ErrorCode::FileNotFound);
    EXPECT_EQ(Apk::open("tests/data/classes.dex").error().code, ErrorCode::InvalidApk);
}

TEST(AxmlTest, NotBinaryXmlIsInvalidXml)
{
    // A DEX file is not a binary XML document: its first chunk type is wrong.
    auto doc = axml::parse(load_buf("tests/data/classes.dex"));
    ASSERT_FALSE(doc.has_value());
    EXPECT_EQ(doc.error().code, ErrorCode::InvalidXml);
    EXPECT_TRUE(doc.error().problems.empty());

    auto tiny = axml::parse(std::vector<uint8_t>{0x03, 0x00});
    ASSERT_FALSE(tiny.has_value());
    EXPECT_EQ(tiny.error().code, ErrorCode::InvalidXml);
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
// Its <application> requires the signature permission, so every component
// without a permission of its own inherits it.  It was built with the Android
// SDK's aapt2 (build-tools 36.0.0), without a classes.dex:
//
//   aapt2 link -o reachable.apk -I android.jar --manifest ReachableManifest.xml
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

    auto component = [&](std::string_view name) {
        const Component *c =
            find_component(*m, std::string("com.dexpp.reach.") + std::string(name));
        EXPECT_NE(c, nullptr) << name;
        return c;
    };
    auto reachable = [&](std::string_view name) {
        const Component *c = component(name);
        return c != nullptr && c->is_reachable();
    };
    auto exported = [&](std::string_view name) {
        const Component *c = component(name);
        return c != nullptr && c->is_exported();
    };

    // Guarded by its own normal/dangerous permission, which a third-party
    // app can hold -> reachable, although the application requires a
    // signature permission.
    EXPECT_TRUE(reachable("NormalActivity"));
    EXPECT_TRUE(reachable("DangerousActivity"));
    EXPECT_TRUE(reachable("NormalReceiver"));
    EXPECT_FALSE(component("NormalActivity")->permission_inherited);
    // Guarded by an undeclared (platform) permission -> still reachable (unknown level).
    EXPECT_TRUE(reachable("PlatformPermActivity"));

    // Guarded by a signature permission the app declares -> not reachable,
    // even though it is exported.
    EXPECT_TRUE(exported("SigActivity"));
    EXPECT_FALSE(reachable("SigActivity"));
    EXPECT_TRUE(component("SigActivity")->guarded_by_signature_permission);
    EXPECT_TRUE(exported("SigService"));
    EXPECT_FALSE(reachable("SigService"));

    // No permission of their own: they take the application's signature
    // permission, so they are not reachable either.
    for (const char *name : {"InheritingActivity", "InheritingService", "InheritingReceiver"}) {
        const Component *c = component(name);
        ASSERT_NE(c, nullptr) << name;
        EXPECT_TRUE(c->is_exported()) << name;
        EXPECT_EQ(c->permission, std::optional<std::string>("com.dexpp.reach.P_SIGNATURE")) << name;
        EXPECT_TRUE(c->permission_inherited) << name;
        EXPECT_EQ(c->permission_reference, std::nullopt) << name;
        EXPECT_FALSE(c->permission_unknown) << name;
        EXPECT_TRUE(c->guarded_by_signature_permission) << name;
        EXPECT_FALSE(c->is_reachable()) << name;
        // Only a provider has read and write permissions.
        EXPECT_EQ(c->read_permission, std::nullopt) << name;
        EXPECT_EQ(c->write_permission, std::nullopt) << name;
    }

    // Not exported -> not reachable.
    EXPECT_FALSE(exported("HiddenActivity"));
    EXPECT_FALSE(reachable("HiddenActivity"));

    // Counts: 5 of 6 activities exported, 3 reachable.
    int act_exported = 0, act_reachable = 0;
    for (const auto *c : m->components_of(Component::Kind::Activity)) {
        act_exported += c->is_exported();
        act_reachable += c->is_reachable();
    }
    EXPECT_EQ(act_exported, 5);
    EXPECT_EQ(act_reachable, 3);
}

TEST(ManifestTest, TheApplicationPermission)
{
    auto apk = Apk::open("tests/data/reachable.apk");
    ASSERT_TRUE(apk.has_value()) << (apk ? "" : apk.error().message);
    const Manifest *m = apk->manifest();
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->application_permission(),
              std::optional<std::string_view>("com.dexpp.reach.P_SIGNATURE"));
    EXPECT_EQ(m->application_permission_reference(), std::nullopt);
    EXPECT_FALSE(m->application_permission_unknown());

    // sample.apk's application has no permission: nothing to inherit.
    auto sample = open_sample();
    const Manifest *s = sample.manifest();
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->application_permission(), std::nullopt);
    for (const Component &c : s->components())
        EXPECT_FALSE(c.permission_inherited) << c.name;
}

TEST(ManifestTest, ProviderReadAndWritePermissions)
{
    // A provider is reached by reading or by writing.  Android gives it a
    // read permission (android:readPermission, else android:permission,
    // else the application's) and a write permission (the same with
    // android:writePermission), and it is guarded only when both are.
    auto apk = Apk::open("tests/data/reachable.apk");
    ASSERT_TRUE(apk.has_value()) << (apk ? "" : apk.error().message);
    const Manifest *m = apk->manifest();
    ASSERT_NE(m, nullptr);
    const std::optional<std::string> signature("com.dexpp.reach.P_SIGNATURE");

    // Nothing of its own: both come from the application.
    const Component *c = find_component(*m, "com.dexpp.reach.InheritingProvider");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->kind, Component::Kind::Provider);
    EXPECT_EQ(c->permission, signature);
    EXPECT_TRUE(c->permission_inherited);
    EXPECT_EQ(c->read_permission, signature);
    EXPECT_EQ(c->write_permission, signature);
    EXPECT_TRUE(c->guarded_by_signature_permission);
    EXPECT_FALSE(c->is_reachable());

    // A normal read permission: any app can read it.
    c = find_component(*m, "com.dexpp.reach.ReadableProvider");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->read_permission, std::optional<std::string>("com.dexpp.reach.P_NORMAL"));
    EXPECT_EQ(c->write_permission, signature);
    EXPECT_FALSE(c->guarded_by_signature_permission);
    EXPECT_TRUE(c->is_reachable());

    // Its own signature permission, but a dangerous write permission.
    c = find_component(*m, "com.dexpp.reach.WritableProvider");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->permission, signature);
    EXPECT_FALSE(c->permission_inherited);
    EXPECT_EQ(c->read_permission, signature);
    EXPECT_EQ(c->write_permission, std::optional<std::string>("com.dexpp.reach.P_DANGEROUS"));
    EXPECT_FALSE(c->guarded_by_signature_permission);
    EXPECT_TRUE(c->is_reachable());
}

// ===== framework attributes are read by resource id =====
//
// Android identifies a framework attribute such as android:exported by its
// resource id, which the document's resource map gives to the attribute's name
// string; the name itself does not count.  These tests change the name strings
// and the map, and check that the manifest reads what Android would read.

namespace {

constexpr uint32_t kExportedId = 0x01010010;
constexpr uint32_t kNameId = 0x01010003;
constexpr const char *kAndroidNs = "http://schemas.android.com/apk/res/android";

/** The binary manifest of a tracked fixture APK. */
std::vector<uint8_t> manifest_bytes(const std::string &apk_path)
{
    std::vector<uint8_t> apk = load_buf(apk_path);
    auto bytes = dex::raw::zip::read_entry(apk, "AndroidManifest.xml");
    EXPECT_TRUE(bytes.has_value()) << apk_path;
    return bytes.value_or(std::vector<uint8_t>{});
}

/** Replace the UTF-16 pool string @p from (with its length prefix) by @p to,
 *  which has the same length.  aapt2 writes manifest string pools in UTF-16. */
bool replace_utf16_string(std::vector<uint8_t> &bytes, const std::string &from,
                          const std::string &to)
{
    auto encode = [](const std::string &text) {
        std::vector<uint8_t> out = {static_cast<uint8_t>(text.size()), 0};
        for (char ch : text) {
            out.push_back(static_cast<uint8_t>(ch));
            out.push_back(0);
        }
        return out;
    };
    std::vector<uint8_t> needle = encode(from);
    std::vector<uint8_t> replacement = encode(to);
    auto it = std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end());
    if (it == bytes.end() || from.size() != to.size())
        return false;
    std::copy(replacement.begin(), replacement.end(), it);
    return true;
}

// ---- a small binary-XML writer, for documents aapt2 would not write ----

struct TestAttr
{
    std::string ns; ///< "" for no namespace.
    std::string name;
    uint32_t resource_id; ///< 0: the name string gets no resource id.
    std::optional<std::string> text = std::nullopt;
    std::optional<bool> boolean = std::nullopt;
    /** Any other typed value, such as a reference; used when `text` is unset. */
    std::optional<axml::TypedValue> typed = std::nullopt;
    /** The raw value's text, when it should differ from `text`. */
    std::optional<std::string> raw = std::nullopt;
};

struct TestElement
{
    std::string tag;
    std::vector<TestAttr> attrs;
    std::vector<TestElement> children;
};

enum class ResourceMap { None, BeforeTheElements, AfterTheFirstElement };

class AxmlWriter
{
  public:
    std::vector<uint8_t> write(const TestElement &root, ResourceMap map)
    {
        // Names with a resource id come first in the pool, in the order of the
        // resource map; every other string comes after them.
        collect_ids(root);
        collect_strings(root);

        std::vector<uint8_t> body;
        append_pool(body);
        if (map == ResourceMap::BeforeTheElements)
            append_resource_map(body);
        append_element(body, root, map == ResourceMap::AfterTheFirstElement);

        std::vector<uint8_t> out;
        put16(out, 0x0003); // RES_XML_TYPE
        put16(out, 8);
        put32(out, static_cast<uint32_t>(8 + body.size()));
        out.insert(out.end(), body.begin(), body.end());
        return out;
    }

  private:
    static void put16(std::vector<uint8_t> &out, uint32_t v)
    {
        out.push_back(static_cast<uint8_t>(v));
        out.push_back(static_cast<uint8_t>(v >> 8));
    }
    static void put32(std::vector<uint8_t> &out, uint32_t v)
    {
        for (int shift = 0; shift < 32; shift += 8)
            out.push_back(static_cast<uint8_t>(v >> shift));
    }

    void collect_ids(const TestElement &element)
    {
        for (const TestAttr &a : element.attrs)
            if (a.resource_id != 0 && !with_id_.contains({a.name, a.resource_id})) {
                with_id_[{a.name, a.resource_id}] = static_cast<uint32_t>(strings_.size());
                strings_.push_back(a.name);
                ids_.push_back(a.resource_id);
            }
        for (const TestElement &child : element.children)
            collect_ids(child);
    }

    uint32_t intern(const std::string &text)
    {
        auto [it, inserted] = plain_.try_emplace(text, static_cast<uint32_t>(strings_.size()));
        if (inserted)
            strings_.push_back(text);
        return it->second;
    }

    void collect_strings(const TestElement &element)
    {
        intern(element.tag);
        for (const TestAttr &a : element.attrs) {
            if (!a.ns.empty())
                intern(a.ns);
            if (a.resource_id == 0)
                intern(a.name);
            if (a.text.has_value())
                intern(*a.text);
            if (a.raw.has_value())
                intern(*a.raw);
        }
        for (const TestElement &child : element.children)
            collect_strings(child);
    }

    uint32_t name_index(const TestAttr &a) const
    {
        return a.resource_id != 0 ? with_id_.at({a.name, a.resource_id}) : plain_.at(a.name);
    }

    void append_pool(std::vector<uint8_t> &out) const
    {
        // UTF-8 pool: each string is its UTF-16 length, its byte length, the
        // bytes and a NUL.  The test strings are short ASCII.
        std::vector<uint8_t> data;
        std::vector<uint32_t> offsets;
        for (const std::string &text : strings_) {
            offsets.push_back(static_cast<uint32_t>(data.size()));
            data.push_back(static_cast<uint8_t>(text.size()));
            data.push_back(static_cast<uint8_t>(text.size()));
            data.insert(data.end(), text.begin(), text.end());
            data.push_back(0);
        }
        while (data.size() % 4 != 0)
            data.push_back(0);
        const uint32_t header = 28;
        const uint32_t strings_start = header + 4 * static_cast<uint32_t>(strings_.size());
        put16(out, 0x0001); // RES_STRING_POOL_TYPE
        put16(out, header);
        put32(out, strings_start + static_cast<uint32_t>(data.size()));
        put32(out, static_cast<uint32_t>(strings_.size()));
        put32(out, 0);     // styles
        put32(out, 0x100); // UTF8_FLAG
        put32(out, strings_start);
        put32(out, 0);
        for (uint32_t offset : offsets)
            put32(out, offset);
        out.insert(out.end(), data.begin(), data.end());
    }

    void append_resource_map(std::vector<uint8_t> &out) const
    {
        put16(out, 0x0180); // RES_XML_RESOURCE_MAP_TYPE
        put16(out, 8);
        put32(out, 8 + 4 * static_cast<uint32_t>(ids_.size()));
        for (uint32_t id : ids_)
            put32(out, id);
    }

    void append_element(std::vector<uint8_t> &out, const TestElement &element, bool map_after_start)
    {
        const uint32_t no_entry = 0xFFFFFFFF;
        put16(out, 0x0102); // RES_XML_START_ELEMENT_TYPE
        put16(out, 16);
        put32(out, 16 + 20 + 20 * static_cast<uint32_t>(element.attrs.size()));
        put32(out, 1);        // line number
        put32(out, no_entry); // comment
        put32(out, no_entry); // element namespace
        put32(out, plain_.at(element.tag));
        put16(out, 20); // attributeStart
        put16(out, 20); // attributeSize
        put16(out, static_cast<uint32_t>(element.attrs.size()));
        put16(out, 0); // idIndex
        put16(out, 0); // classIndex
        put16(out, 0); // styleIndex
        for (const TestAttr &a : element.attrs) {
            put32(out, a.ns.empty() ? no_entry : plain_.at(a.ns));
            put32(out, name_index(a));
            if (a.raw.has_value())
                put32(out, plain_.at(*a.raw));
            else
                put32(out, a.text.has_value() ? plain_.at(*a.text) : no_entry);
            put16(out, 8); // Res_value size
            out.push_back(0);
            if (a.text.has_value()) {
                out.push_back(0x03); // TYPE_STRING
                put32(out, plain_.at(*a.text));
            }
            else if (a.typed.has_value()) {
                out.push_back(a.typed->type);
                put32(out, a.typed->data);
            }
            else {
                out.push_back(0x12); // TYPE_INT_BOOLEAN
                put32(out, a.boolean.value_or(false) ? 0xFFFFFFFF : 0);
            }
        }
        if (map_after_start)
            append_resource_map(out);
        for (const TestElement &child : element.children)
            append_element(out, child, false);
        put16(out, 0x0103); // RES_XML_END_ELEMENT_TYPE
        put16(out, 16);
        put32(out, 24);
        put32(out, 1);
        put32(out, no_entry);
        put32(out, no_entry);
        put32(out, plain_.at(element.tag));
    }

    std::vector<std::string> strings_;
    std::vector<uint32_t> ids_;
    std::map<std::pair<std::string, uint32_t>, uint32_t> with_id_;
    std::map<std::string, uint32_t> plain_;
};

/** A manifest with one activity, ".A", that has @p attrs besides its name. */
Manifest one_activity(std::vector<TestAttr> attrs, bool with_intent_filter, ResourceMap map)
{
    TestElement activity{"activity", {{kAndroidNs, "name", kNameId, ".A"}}, {}};
    for (TestAttr &a : attrs)
        activity.attrs.push_back(std::move(a));
    if (with_intent_filter)
        activity.children.push_back(
            {"intent-filter",
             {},
             {{"action", {{kAndroidNs, "name", kNameId, "android.intent.action.VIEW"}}, {}}}});
    TestElement root{
        "manifest", {{"", "package", 0, "com.test"}}, {{"application", {}, {std::move(activity)}}}};
    auto doc = axml::parse(AxmlWriter().write(root, map));
    EXPECT_TRUE(doc.has_value()) << (doc ? "" : doc.error().message);
    return Manifest(std::move(*doc));
}

const Component &only_component(const Manifest &m)
{
    EXPECT_EQ(m.components().size(), 1u);
    return m.components().front();
}

} // namespace

TEST(ManifestAttributes, EachAttributeHasItsResourceId)
{
    auto doc = axml::parse(manifest_bytes("tests/data/sample.apk"));
    ASSERT_TRUE(doc.has_value());
    EXPECT_TRUE(doc->has_resource_map);
    const axml::XmlNode *activity = doc->root.child("application")->child("activity");
    ASSERT_NE(activity, nullptr);
    ASSERT_NE(activity->attribute("exported"), nullptr);
    EXPECT_EQ(activity->attribute("exported")->resource_id, kExportedId);
    EXPECT_EQ(activity->attribute_by_id(kExportedId), activity->attribute("exported"));
    EXPECT_EQ(activity->attribute_by_id(0), nullptr);
    // `package` is not a framework attribute: no namespace, no resource id.
    ASSERT_NE(doc->root.attribute("package"), nullptr);
    EXPECT_EQ(doc->root.attribute("package")->resource_id, 0u);
}

TEST(ManifestAttributes, ARenamedNameStringDoesNotHideTheValue)
{
    // sample.apk's manifest, with the name string of android:exported changed
    // to another string of the same length.  The resource map still gives it
    // the id of android:exported, so Android still reads it.
    std::vector<uint8_t> bytes = manifest_bytes("tests/data/sample.apk");
    ASSERT_TRUE(replace_utf16_string(bytes, "exported", "exportex"));
    auto doc = axml::parse(bytes);
    ASSERT_TRUE(doc.has_value());
    Manifest m(std::move(*doc));

    const Component *main = find_component(m, "com.example.dexpp.MainActivity");
    const Component *sync = find_component(m, "com.example.dexpp.SyncService");
    const Component *provider = find_component(m, "com.example.dexpp.DataProvider");
    ASSERT_TRUE(main && sync && provider);
    EXPECT_EQ(main->exported, std::optional<bool>(true));
    EXPECT_EQ(sync->exported, std::optional<bool>(false));
    EXPECT_EQ(provider->exported, std::optional<bool>(false));
    EXPECT_EQ(m.package(), "com.example.dexpp");
    EXPECT_EQ(m.version_code(), 7);
}

TEST(ManifestAttributes, AnAttributeWithTheIdIsReadWhateverItsName)
{
    // The attribute is called "visible", but its resource id is the one of
    // android:exported.  It says false, so the intent filter does not matter.
    Manifest m = one_activity({{kAndroidNs, "visible", kExportedId, std::nullopt, false}},
                              /*with_intent_filter=*/true, ResourceMap::BeforeTheElements);
    EXPECT_EQ(only_component(m).exported, std::optional<bool>(false));
    EXPECT_FALSE(only_component(m).is_exported());
    EXPECT_EQ(only_component(m).name, "com.test.A");
}

TEST(ManifestAttributes, AnAttributeOnlyNamedExportedIsIgnored)
{
    // Named exported, in the android namespace, but without a resource id:
    // Android does not see it as android:exported, so neither does dexpp.
    Manifest m = one_activity({{kAndroidNs, "exported", 0, std::nullopt, true}},
                              /*with_intent_filter=*/false, ResourceMap::BeforeTheElements);
    EXPECT_EQ(only_component(m).exported, std::nullopt);
    EXPECT_FALSE(only_component(m).is_exported());
}

TEST(ManifestAttributes, WithoutAResourceMapTheNameIsUsed)
{
    // A document without a resource map was not made by aapt or aapt2.  dexpp
    // then reads attributes by name, in the android namespace only.
    Manifest android_ns = one_activity({{kAndroidNs, "exported", 0, std::nullopt, true}},
                                       /*with_intent_filter=*/false, ResourceMap::None);
    EXPECT_EQ(only_component(android_ns).exported, std::optional<bool>(true));
    EXPECT_EQ(only_component(android_ns).name, "com.test.A");

    Manifest other_ns = one_activity({{"http://example.com/ns", "exported", 0, std::nullopt, true}},
                                     /*with_intent_filter=*/false, ResourceMap::None);
    EXPECT_EQ(only_component(other_ns).exported, std::nullopt);
}

TEST(ManifestAttributes, AResourceMapAfterTheFirstElementIsIgnored)
{
    // Android reads the resource map only before the first element, so the id
    // this late map gives to "visible" does not count.
    Manifest m = one_activity({{kAndroidNs, "visible", kExportedId, std::nullopt, true}},
                              /*with_intent_filter=*/false, ResourceMap::AfterTheFirstElement);
    EXPECT_EQ(only_component(m).exported, std::nullopt);
}

// ===== attribute values given by resource references =====
//
// tests/data/refs.apk sets its attributes through resource references, such
// as android:exported="@bool/yes".  Its sources are RefsManifest.xml,
// RefsValues.xml (res/values/values.xml), RefsValuesV31.xml
// (res/values-v31/values.xml), RefsValuesFr.xml (res/values-fr/values.xml)
// and RefsValuesSrLatnLandNight.xml
// (res/values-b+sr+Latn-land-night/values.xml).  It was built with the
// Android SDK's aapt2 (build-tools 36.0.0) against
// platforms/android-34/android.jar:
//
//   aapt2 compile --dir res -o compiled.zip
//   aapt2 link -o refs.apk -I android.jar --manifest RefsManifest.xml compiled.zip
//   touch -t 198001010000 classes.dex       # tests/data/classes.dex
//   TZ=UTC zip -X -j refs.apk classes.dex
//
// aapt2 writes `@null` as a reference to id 0 and `@empty` as TYPE_NULL with
// DATA_NULL_EMPTY, in the manifest and in the resource table alike.

namespace {

constexpr uint32_t kVersionCodeId = 0x0101021b;
constexpr uint32_t kVersionNameId = 0x0101021c;
constexpr uint32_t kPermissionId = 0x01010006;
constexpr uint32_t kDebuggableId = 0x0101000f;
constexpr uint32_t kAllowBackupId = 0x01010280;
constexpr uint8_t kTypeReference = 0x01;
constexpr uint8_t kTypeIntDec = 0x10;

Apk open_refs()
{
    auto apk = Apk::open("tests/data/refs.apk");
    EXPECT_TRUE(apk.has_value()) << (apk ? "" : apk.error().message);
    return std::move(*apk);
}

uint32_t resource_id(const Apk &apk, std::string_view type, std::string_view entry)
{
    const ResourceTable *table = apk.resources();
    EXPECT_NE(table, nullptr);
    auto id = table ? table->id_of(type, entry) : std::nullopt;
    EXPECT_TRUE(id.has_value()) << type << "/" << entry;
    return id.value_or(0);
}

/** A <manifest> with @p attrs and one activity ".A" with @p activity_attrs. */
axml::XmlDocument crafted_manifest(std::vector<TestAttr> attrs,
                                   std::vector<TestAttr> activity_attrs)
{
    TestElement activity{"activity", {{kAndroidNs, "name", kNameId, ".A"}}, {}};
    for (TestAttr &a : activity_attrs)
        activity.attrs.push_back(std::move(a));
    TestElement root{"manifest", {{"", "package", 0, "com.test"}}, {}};
    for (TestAttr &a : attrs)
        root.attrs.push_back(std::move(a));
    root.children.push_back({"application", {}, {std::move(activity)}});
    auto doc = axml::parse(AxmlWriter().write(root, ResourceMap::BeforeTheElements));
    EXPECT_TRUE(doc.has_value()) << (doc ? "" : doc.error().message);
    return doc ? std::move(*doc) : axml::XmlDocument{};
}

TestAttr reference_attr(std::string name, uint32_t attribute_id, uint32_t target)
{
    return TestAttr{kAndroidNs,   std::move(name), attribute_id,
                    std::nullopt, std::nullopt,    axml::TypedValue{kTypeReference, target}};
}

} // namespace

TEST(ManifestReferences, BooleansAndIntegersAreResolved)
{
    Apk apk = open_refs();
    const Manifest *m = apk.manifest();
    ASSERT_NE(m, nullptr);

    EXPECT_EQ(m->min_sdk(), 23);
    EXPECT_EQ(m->min_sdk_reference(), resource_id(apk, "integer", "min_sdk"));
    EXPECT_EQ(m->target_sdk(), 34);
    EXPECT_EQ(m->target_sdk_reference(), resource_id(apk, "integer", "target_sdk"));
    // debug is true by default and false in values-v31; allow_backup is
    // false by default and true in values-v31.  The value is the default
    // configuration's, and the other one is reported as a variant.
    EXPECT_EQ(m->debuggable(), std::optional<bool>(true));
    EXPECT_EQ(m->debuggable_reference(), resource_id(apk, "bool", "debug"));
    EXPECT_TRUE(m->debuggable_varies());
    EXPECT_EQ(m->allow_backup(), std::optional<bool>(false));
    EXPECT_EQ(m->allow_backup_reference(), resource_id(apk, "bool", "allow_backup"));
    EXPECT_TRUE(m->allow_backup_varies());

    // A plain value has no reference.
    EXPECT_EQ(m->version_code(), 42);
    EXPECT_EQ(m->version_code_reference(), std::nullopt);

    // A text attribute: @string/version_name.
    EXPECT_EQ(m->version_name(), std::optional<std::string_view>("1.0"));
    EXPECT_EQ(m->version_name_reference(), resource_id(apk, "string", "version_name"));
}

TEST(ManifestReferences, ExportedIsResolved)
{
    Apk apk = open_refs();
    const Manifest *m = apk.manifest();
    ASSERT_NE(m, nullptr);

    // No intent filter, so without the reference it would read as not
    // exported.
    const Component *c = find_component(*m, "com.dexpp.refs.ExportedActivity");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->exported, std::optional<bool>(true));
    EXPECT_EQ(c->exported_reference, resource_id(apk, "bool", "yes"));
    EXPECT_TRUE(c->is_exported());
    EXPECT_TRUE(c->is_reachable());

    // An intent filter, but the reference says false.
    c = find_component(*m, "com.dexpp.refs.HiddenActivity");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->exported, std::optional<bool>(false));
    EXPECT_FALSE(c->is_exported());
    EXPECT_FALSE(c->is_reachable());

    // @bool/same_as_yes is itself @bool/yes.
    c = find_component(*m, "com.dexpp.refs.ChainedActivity");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->exported, std::optional<bool>(true));

    // @integer/one: Android reads an integer as a boolean, nonzero is true.
    c = find_component(*m, "com.dexpp.refs.IntegerActivity");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->exported, std::optional<bool>(true));

    // None of these has a value that changes with the configuration, and
    // every value is known.
    for (const char *name :
         {"ExportedActivity", "HiddenActivity", "ChainedActivity", "IntegerActivity"}) {
        c = find_component(*m, std::string("com.dexpp.refs.") + name);
        ASSERT_NE(c, nullptr) << name;
        EXPECT_FALSE(c->exported_varies) << name;
        EXPECT_FALSE(c->exported_unknown) << name;
    }
}

TEST(ManifestReferences, AnExportedThatVariesCountsAsExported)
{
    Apk apk = open_refs();
    const Manifest *m = apk.manifest();
    ASSERT_NE(m, nullptr);

    // false in the default configuration, true in values-v31: exported on
    // Android 12 and newer.  The value is the default configuration's, and
    // the effective answer is the unsafe one.
    for (const char *name : {"VariantActivity", "ChainedVariantActivity"}) {
        const Component *c = find_component(*m, std::string("com.dexpp.refs.") + name);
        ASSERT_NE(c, nullptr) << name;
        EXPECT_EQ(c->exported, std::optional<bool>(false)) << name;
        EXPECT_TRUE(c->exported_varies) << name;
        EXPECT_TRUE(c->is_exported()) << name;
        EXPECT_TRUE(c->is_reachable()) << name;
    }
    EXPECT_EQ(find_component(*m, "com.dexpp.refs.VariantActivity")->exported_reference,
              resource_id(apk, "bool", "differs_on_v31"));
}

TEST(ManifestReferences, StringResourcesAreReadAsBooleansLikeAndroid)
{
    // TypedArray.getBoolean reads a string with XmlUtils.convertValueToBoolean:
    // exactly "1", "true" and "TRUE" are true, other text is false, and
    // empty text gives the default, false for android:exported.
    Apk apk = open_refs();
    const Manifest *m = apk.manifest();
    ASSERT_NE(m, nullptr);
    const std::vector<std::pair<std::string, bool>> cases = {
        {"TextActivity", true},         // "true"
        {"UpperTextActivity", true},    // "TRUE"
        {"OneTextActivity", true},      // "1"
        {"CapitalTextActivity", false}, // "True", with an intent filter
        {"YesTextActivity", false},     // "yes"
        {"EmptyTextActivity", false},   // "", with an intent filter
    };
    for (const auto &[name, expected] : cases) {
        const Component *c = find_component(*m, "com.dexpp.refs." + name);
        ASSERT_NE(c, nullptr) << name;
        EXPECT_EQ(c->exported, std::optional<bool>(expected)) << name;
        EXPECT_EQ(c->is_exported(), expected) << name;
        EXPECT_FALSE(c->exported_varies) << name;
    }
    EXPECT_EQ(find_component(*m, "com.dexpp.refs.TextActivity")->exported_reference,
              resource_id(apk, "string", "not_a_bool"));
}

TEST(ManifestReferences, AnUnresolvedExportedCountsAsExported)
{
    Apk apk = open_refs();
    const Manifest *m = apk.manifest();
    ASSERT_NE(m, nullptr);

    // ?android:attr/windowFullscreen: a theme attribute, unknown without the
    // theme.
    const Component *c = find_component(*m, "com.dexpp.refs.ThemeActivity");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->exported, std::nullopt);
    EXPECT_EQ(c->exported_reference, 0x0101020du);
    EXPECT_TRUE(c->exported_unknown);
    EXPECT_FALSE(c->exported_varies);
    EXPECT_TRUE(c->is_exported());
    EXPECT_TRUE(c->is_reachable());
}

TEST(ManifestReferences, ProtectionLevelsAreResolved)
{
    Apk apk = open_refs();
    const Manifest *m = apk.manifest();
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(
        m->declared_permissions(),
        (std::vector<std::string>{"com.dexpp.refs.P_SIGNATURE", "com.dexpp.refs.P_TEXT",
                                  "com.dexpp.refs.P_VARYING", "com.dexpp.refs.P_BY_REFERENCE"}));

    // @integer/signature_level is 2, signature: the service is guarded.
    EXPECT_EQ(m->declared_permission_protection_level("com.dexpp.refs.P_SIGNATURE"), 2);
    EXPECT_FALSE(m->declared_permission_protection_level_varies("com.dexpp.refs.P_SIGNATURE"));
    const Component *c = find_component(*m, "com.dexpp.refs.GuardedService");
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->guarded_by_signature_permission);
    EXPECT_FALSE(c->is_reachable());

    // @string/not_a_number: the level is unknown, so it does not guard.
    EXPECT_EQ(m->declared_permission_protection_level("com.dexpp.refs.P_TEXT"), std::nullopt);
    c = find_component(*m, "com.dexpp.refs.TextGuardedService");
    ASSERT_NE(c, nullptr);
    EXPECT_FALSE(c->guarded_by_signature_permission);
    EXPECT_TRUE(c->is_reachable());

    // @integer/level_on_v31 is 2 (signature) by default and 0 (normal) in
    // values-v31.  On Android 12 and newer any app can hold the permission,
    // so it does not guard.
    EXPECT_EQ(m->declared_permission_protection_level("com.dexpp.refs.P_VARYING"), 2);
    EXPECT_TRUE(m->declared_permission_protection_level_varies("com.dexpp.refs.P_VARYING"));
    c = find_component(*m, "com.dexpp.refs.VaryingLevelService");
    ASSERT_NE(c, nullptr);
    EXPECT_FALSE(c->guarded_by_signature_permission);
    EXPECT_TRUE(c->is_reachable());

    EXPECT_FALSE(m->declared_permission_protection_level_varies("android.permission.DUMP"));
}

TEST(ManifestReferences, PermissionsGivenByReference)
{
    Apk apk = open_refs();
    const Manifest *m = apk.manifest();
    ASSERT_NE(m, nullptr);

    // android:permission="@string/signature_permission", one value: resolved,
    // and it names a signature permission the app declares.
    const Component *c = find_component(*m, "com.dexpp.refs.PermissionByReferenceService");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->permission, std::optional<std::string>("com.dexpp.refs.P_SIGNATURE"));
    EXPECT_EQ(c->permission_reference, resource_id(apk, "string", "signature_permission"));
    EXPECT_FALSE(c->permission_unknown);
    EXPECT_TRUE(c->guarded_by_signature_permission);
    EXPECT_FALSE(c->is_reachable());

    // The string has a res/values-fr variant (with the same text).  Android
    // reads android:permission with getNonConfigurationString(attr, 0), which
    // returns null for a value that changes with the locale: no permission,
    // and that is known.
    c = find_component(*m, "com.dexpp.refs.LocalizedPermissionService");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->permission, std::nullopt);
    EXPECT_EQ(c->permission_reference, resource_id(apk, "string", "localized_permission"));
    EXPECT_FALSE(c->permission_unknown);
    EXPECT_FALSE(c->permission_inherited);
    EXPECT_FALSE(c->guarded_by_signature_permission);
    EXPECT_TRUE(c->is_reachable());

    // P_SIGNATURE by default, P_TEXT in values-v31: Android reads it, with
    // the value of the device's version, so there is no single value.
    c = find_component(*m, "com.dexpp.refs.PermissionOnV31Service");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->permission, std::nullopt);
    EXPECT_EQ(c->permission_reference, resource_id(apk, "string", "permission_on_v31"));
    EXPECT_TRUE(c->permission_unknown);
    EXPECT_TRUE(c->is_reachable());

    // <permission android:name="@string/declared_name"
    // android:protectionLevel="signature">: the declared name is resolved,
    // so the literal android:permission matches it.
    EXPECT_EQ(m->declared_permission_protection_level("com.dexpp.refs.P_BY_REFERENCE"), 2);
    c = find_component(*m, "com.dexpp.refs.DeclaredByReferenceService");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->permission_reference, std::nullopt);
    EXPECT_TRUE(c->guarded_by_signature_permission);
    EXPECT_FALSE(c->is_reachable());
}

TEST(ManifestReferences, WithoutTheResourceTableOnlyTheReferenceIsKnown)
{
    // A Manifest built from the document alone cannot resolve anything: every
    // value is unknown, but no reference reads as "not written".
    auto doc = axml::parse(manifest_bytes("tests/data/refs.apk"));
    ASSERT_TRUE(doc.has_value());
    Manifest m(std::move(*doc));
    Apk apk = open_refs();

    EXPECT_EQ(m.min_sdk(), std::nullopt);
    EXPECT_EQ(m.min_sdk_reference(), resource_id(apk, "integer", "min_sdk"));
    EXPECT_TRUE(m.min_sdk_unknown());
    EXPECT_TRUE(m.target_sdk_unknown());
    EXPECT_EQ(m.debuggable(), std::nullopt);
    EXPECT_EQ(m.debuggable_reference(), resource_id(apk, "bool", "debug"));
    EXPECT_TRUE(m.debuggable_unknown());
    EXPECT_TRUE(m.allow_backup_unknown());
    EXPECT_FALSE(m.debuggable_varies());
    EXPECT_EQ(m.version_code(), 42);
    EXPECT_FALSE(m.version_code_unknown());
    EXPECT_EQ(m.version_name(), std::nullopt);
    EXPECT_EQ(m.version_name_reference(), resource_id(apk, "string", "version_name"));
    EXPECT_TRUE(m.version_name_unknown());
    // The permission declared by reference has no name: it is left out.
    EXPECT_EQ(m.declared_permissions().size(), 3u);

    const Component *hidden = find_component(m, "com.dexpp.refs.HiddenActivity");
    ASSERT_NE(hidden, nullptr);
    EXPECT_EQ(hidden->exported, std::nullopt);
    EXPECT_EQ(hidden->exported_reference, resource_id(apk, "bool", "no"));
    EXPECT_TRUE(hidden->exported_unknown);
    EXPECT_TRUE(hidden->is_exported()); // unknown: possibly exported

    // Without the table, a reference to a null value is unknown too.
    const Component *null_activity = find_component(m, "com.dexpp.refs.NullActivity");
    ASSERT_NE(null_activity, nullptr);
    EXPECT_TRUE(null_activity->exported_unknown);
    EXPECT_TRUE(null_activity->is_exported());

    // The signature level is unknown too, so the service is not guarded.
    EXPECT_EQ(m.declared_permission_protection_level("com.dexpp.refs.P_SIGNATURE"), std::nullopt);
    const Component *guarded = find_component(m, "com.dexpp.refs.GuardedService");
    ASSERT_NE(guarded, nullptr);
    EXPECT_FALSE(guarded->guarded_by_signature_permission);
    EXPECT_TRUE(guarded->is_reachable());

    const Component *by_reference =
        find_component(m, "com.dexpp.refs.PermissionByReferenceService");
    ASSERT_NE(by_reference, nullptr);
    EXPECT_EQ(by_reference->permission, std::nullopt);
    EXPECT_EQ(by_reference->permission_reference,
              resource_id(apk, "string", "signature_permission"));
    EXPECT_TRUE(by_reference->permission_unknown);
    EXPECT_TRUE(by_reference->is_reachable());
}

TEST(ManifestReferences, AMissingIdIsNotResolved)
{
    // aapt2 does not link a reference to a missing resource, nor a reference
    // for android:versionCode; build such a manifest by hand and read it with
    // refs.apk's resource table.
    Apk apk = open_refs();
    const ResourceTable *table = apk.resources();
    ASSERT_NE(table, nullptr);
    const uint32_t missing = 0x7f990000;
    const uint32_t min_sdk = resource_id(apk, "integer", "min_sdk");

    Manifest known(crafted_manifest({reference_attr("versionCode", kVersionCodeId, min_sdk)},
                                    {reference_attr("exported", kExportedId, missing)}),
                   table);
    EXPECT_EQ(known.version_code(), 23);
    EXPECT_EQ(known.version_code_reference(), min_sdk);
    EXPECT_FALSE(known.version_code_unknown());
    const Component &c = only_component(known);
    EXPECT_EQ(c.exported, std::nullopt);
    EXPECT_EQ(c.exported_reference, missing);
    EXPECT_TRUE(c.exported_unknown);
    EXPECT_TRUE(c.is_exported());

    Manifest unknown(crafted_manifest({reference_attr("versionCode", kVersionCodeId, missing)}, {}),
                     table);
    EXPECT_EQ(unknown.version_code(), std::nullopt);
    EXPECT_EQ(unknown.version_code_reference(), missing);
    EXPECT_TRUE(unknown.version_code_unknown());

    // A plain integer is still read as before.
    Manifest plain(
        crafted_manifest({TestAttr{kAndroidNs, "versionCode", kVersionCodeId, std::nullopt,
                                   std::nullopt, axml::TypedValue{kTypeIntDec, 5}}},
                         {}),
        table);
    EXPECT_EQ(plain.version_code(), 5);
    EXPECT_EQ(plain.version_code_reference(), std::nullopt);
}

TEST(ManifestReferences, PlainValuesHaveNoReference)
{
    auto apk = open_sample();
    const Manifest *m = apk.manifest();
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->debuggable_reference(), std::nullopt);
    EXPECT_EQ(m->allow_backup_reference(), std::nullopt);
    EXPECT_EQ(m->min_sdk_reference(), std::nullopt);
    EXPECT_EQ(m->target_sdk_reference(), std::nullopt);
    EXPECT_EQ(m->version_code_reference(), std::nullopt);
    EXPECT_EQ(m->version_name_reference(), std::nullopt);
    EXPECT_EQ(m->application_permission_reference(), std::nullopt);
    EXPECT_FALSE(m->debuggable_varies());
    EXPECT_FALSE(m->allow_backup_varies());
    EXPECT_FALSE(m->debuggable_unknown());
    EXPECT_FALSE(m->allow_backup_unknown());
    EXPECT_FALSE(m->version_code_unknown());
    EXPECT_FALSE(m->version_name_unknown());
    EXPECT_FALSE(m->min_sdk_unknown());
    EXPECT_FALSE(m->target_sdk_unknown());
    EXPECT_FALSE(m->application_permission_unknown());
    for (const Component &c : m->components()) {
        EXPECT_EQ(c.exported_reference, std::nullopt) << c.name;
        EXPECT_EQ(c.name_reference, std::nullopt) << c.name;
        EXPECT_EQ(c.permission_reference, std::nullopt) << c.name;
        EXPECT_EQ(c.read_permission_reference, std::nullopt) << c.name;
        EXPECT_EQ(c.write_permission_reference, std::nullopt) << c.name;
        EXPECT_FALSE(c.exported_varies) << c.name;
        EXPECT_FALSE(c.exported_unknown) << c.name;
        EXPECT_FALSE(c.name_unknown) << c.name;
        EXPECT_FALSE(c.permission_unknown) << c.name;
        EXPECT_FALSE(c.read_permission_unknown) << c.name;
        EXPECT_FALSE(c.write_permission_unknown) << c.name;
    }
}

// ===== literal values written as strings =====
//
// aapt2 always writes android:exported="true" as a typed boolean, but a
// hand-made binary manifest can store the string "true".  Android reads it
// with TypedArray.getBoolean, as it reads a string resource.

namespace {

TestAttr string_attr(std::string name, uint32_t id, std::string text)
{
    return TestAttr{kAndroidNs, std::move(name), id, std::move(text)};
}

} // namespace

TEST(ManifestStrings, ExportedAsAString)
{
    const std::vector<std::pair<std::string, bool>> cases = {
        {"true", true}, {"TRUE", true},   {"1", true},  {"True", false},
        {"yes", false}, {"false", false}, {"0", false},
    };
    for (const auto &[text, expected] : cases) {
        Manifest m = one_activity({string_attr("exported", kExportedId, text)},
                                  /*with_intent_filter=*/!expected, ResourceMap::BeforeTheElements);
        EXPECT_EQ(only_component(m).exported, std::optional<bool>(expected)) << text;
        EXPECT_EQ(only_component(m).exported_reference, std::nullopt) << text;
        EXPECT_EQ(only_component(m).is_exported(), expected) << text;
    }
    // Empty text gives getBoolean's default, false: the flag is set (so the
    // intent filter does not make it exported).
    Manifest empty = one_activity({string_attr("exported", kExportedId, "")},
                                  /*with_intent_filter=*/true, ResourceMap::BeforeTheElements);
    EXPECT_EQ(only_component(empty).exported, std::optional<bool>(false));
    EXPECT_FALSE(only_component(empty).is_exported());
}

TEST(ManifestStrings, EmptyTextGivesTheAttributeDefault)
{
    // ParsingPackageUtils reads debuggable with the default false and
    // allowBackup with the default true.
    auto doc = axml::parse(
        AxmlWriter().write(TestElement{"manifest",
                                       {{"", "package", 0, "com.test"}},
                                       {{"application",
                                         {string_attr("debuggable", kDebuggableId, ""),
                                          string_attr("allowBackup", kAllowBackupId, "")},
                                         {}}}},
                           ResourceMap::BeforeTheElements));
    ASSERT_TRUE(doc.has_value());
    Manifest m(std::move(*doc));
    EXPECT_EQ(m.debuggable(), std::optional<bool>(false));
    EXPECT_EQ(m.allow_backup(), std::optional<bool>(true));

    doc = axml::parse(
        AxmlWriter().write(TestElement{"manifest",
                                       {{"", "package", 0, "com.test"}},
                                       {{"application",
                                         {string_attr("debuggable", kDebuggableId, "TRUE"),
                                          string_attr("allowBackup", kAllowBackupId, "no")},
                                         {}}}},
                           ResourceMap::BeforeTheElements));
    ASSERT_TRUE(doc.has_value());
    Manifest set(std::move(*doc));
    EXPECT_EQ(set.debuggable(), std::optional<bool>(true));
    EXPECT_EQ(set.allow_backup(), std::optional<bool>(false));
}

TEST(ManifestStrings, TheTypedValueWinsOverTheRawValue)
{
    // An attribute holds two string indexes: the raw value and the typed
    // value.  aapt2 writes the same index in both; this one says "false" in
    // the raw value and "true" in the typed value.  TypedArray reads the
    // typed value, so Android sees an exported activity.
    TestAttr exported = string_attr("exported", kExportedId, "true");
    exported.raw = "false";
    TestAttr permission = string_attr("permission", kPermissionId, "com.test.REAL");
    permission.raw = "com.test.DECOY";
    Manifest m = one_activity({exported, permission}, /*with_intent_filter=*/false,
                              ResourceMap::BeforeTheElements);
    EXPECT_EQ(only_component(m).exported, std::optional<bool>(true));
    EXPECT_EQ(only_component(m).permission, std::optional<std::string>("com.test.REAL"));
}

// ===== text and booleans resolved against a crafted resource table =====

namespace {

/** A manifest with one activity whose android:name is @p name and whose
 *  other attributes are @p attrs, read with @p table. */
Manifest crafted_with_table(TestAttr name, std::vector<TestAttr> attrs,
                            const std::vector<uint8_t> &table_bytes,
                            std::vector<TestAttr> root_attrs = {},
                            std::vector<TestElement> extra_children = {})
{
    TestElement activity{"activity", {std::move(name)}, {}};
    for (TestAttr &a : attrs)
        activity.attrs.push_back(std::move(a));
    TestElement root{"manifest", {{"", "package", 0, "com.test"}}, std::move(extra_children)};
    for (TestAttr &a : root_attrs)
        root.attrs.push_back(std::move(a));
    root.children.push_back({"application", {}, {std::move(activity)}});
    auto doc = axml::parse(AxmlWriter().write(root, ResourceMap::BeforeTheElements));
    EXPECT_TRUE(doc.has_value());
    auto table = ResourceTable::parse(table_bytes);
    EXPECT_TRUE(table.has_value());
    return Manifest(doc ? std::move(*doc) : axml::XmlDocument{}, table ? &*table : nullptr);
}

TestAttr literal_name(std::string text) { return string_attr("name", kNameId, std::move(text)); }

using test::ArscBuilder;
using test::ArscConfig;

} // namespace

TEST(ManifestTextReferences, AComponentNameGivenByReference)
{
    // aapt2 refuses a reference for a component's android:name, but Android
    // resolves one (getNonConfigurationString(name, 0)).
    ArscBuilder b;
    b.value(0, ArscConfig(), ArscBuilder::kString, b.string("com.test.Named"));
    b.value(1, ArscConfig(), ArscBuilder::kString, b.string("com.test.Default"));
    b.value(1, ArscConfig().language('f', 'r'), ArscBuilder::kString, b.string("com.test.Fr"));
    b.spec_flags(1, 0x0004); // varies with the locale, as aapt2 would mark it
    const std::vector<uint8_t> table = b.build();

    Manifest named =
        crafted_with_table(reference_attr("name", kNameId, ArscBuilder::id(0)), {}, table);
    EXPECT_EQ(only_component(named).name, "com.test.Named");
    EXPECT_EQ(only_component(named).name_reference, ArscBuilder::id(0));
    EXPECT_FALSE(only_component(named).name_unknown);

    // A name that changes with the locale: Android ignores it.
    Manifest localized =
        crafted_with_table(reference_attr("name", kNameId, ArscBuilder::id(1)), {}, table);
    EXPECT_EQ(only_component(localized).name, "");
    EXPECT_EQ(only_component(localized).name_reference, ArscBuilder::id(1));
    EXPECT_FALSE(only_component(localized).name_unknown);

    // A missing id.
    Manifest missing =
        crafted_with_table(reference_attr("name", kNameId, ArscBuilder::id(9)), {}, table);
    EXPECT_EQ(only_component(missing).name, "");
    EXPECT_EQ(only_component(missing).name_reference, ArscBuilder::id(9));
    EXPECT_TRUE(only_component(missing).name_unknown);
}

TEST(ManifestTextReferences, TheTypeSpecFlagsDecideLikeAndroid)
{
    // Android looks at the typeSpec flags, not at the values: a string with
    // one value but flags that say "changes with the locale" is ignored, and
    // one whose flags only say "changes with the Android version" is read.
    ArscBuilder b;
    const uint32_t guard = b.string("com.test.GUARD");
    b.value(0, ArscConfig(), ArscBuilder::kString, guard).spec_flags(0, 0x0004);
    b.value(1, ArscConfig(), ArscBuilder::kString, guard).spec_flags(1, 0x0400);
    b.value(2, ArscConfig(), ArscBuilder::kString, guard);
    b.value(2, ArscConfig().sdk(31), ArscBuilder::kString, guard).spec_flags(2, 0x0400);
    const std::vector<uint8_t> table = b.build();
    TestElement declared{"permission",
                         {literal_name("com.test.GUARD"),
                          TestAttr{kAndroidNs, "protectionLevel", 0x01010009, std::nullopt,
                                   std::nullopt, axml::TypedValue{0x11, 2}}},
                         {}};

    Manifest ignored = crafted_with_table(
        literal_name(".A"), {reference_attr("permission", kPermissionId, ArscBuilder::id(0))},
        table, {}, {declared});
    EXPECT_EQ(only_component(ignored).permission, std::nullopt);
    EXPECT_FALSE(only_component(ignored).permission_unknown);
    EXPECT_FALSE(only_component(ignored).guarded_by_signature_permission);

    Manifest version_only = crafted_with_table(
        literal_name(".A"), {reference_attr("permission", kPermissionId, ArscBuilder::id(1))},
        table, {}, {declared});
    EXPECT_EQ(only_component(version_only).permission,
              std::optional<std::string>("com.test.GUARD"));
    EXPECT_TRUE(only_component(version_only).guarded_by_signature_permission);

    // The same text in the default and v31 configurations: one value.
    Manifest same_text = crafted_with_table(
        literal_name(".A"), {reference_attr("permission", kPermissionId, ArscBuilder::id(2))},
        table, {}, {declared});
    EXPECT_EQ(only_component(same_text).permission, std::optional<std::string>("com.test.GUARD"));
}

TEST(ManifestTextReferences, VersionNameAndUsesPermission)
{
    ArscBuilder b;
    b.value(0, ArscConfig(), ArscBuilder::kString, b.string("2.5"));
    b.value(1, ArscConfig(), ArscBuilder::kString, b.string("android.permission.CAMERA"));
    const std::vector<uint8_t> table = b.build();
    TestElement uses_by_reference{
        "uses-permission", {reference_attr("name", kNameId, ArscBuilder::id(1))}, {}};
    TestElement uses_literal{"uses-permission", {literal_name("android.permission.INTERNET")}, {}};
    Manifest m =
        crafted_with_table(literal_name(".A"), {}, table,
                           {reference_attr("versionName", kVersionNameId, ArscBuilder::id(0))},
                           {uses_by_reference, uses_literal});
    EXPECT_EQ(m.version_name(), std::optional<std::string_view>("2.5"));
    EXPECT_EQ(m.version_name_reference(), ArscBuilder::id(0));
    EXPECT_FALSE(m.version_name_unknown());
    // Android reads <uses-permission android:name> with getNonResourceString,
    // which ignores a reference.
    EXPECT_EQ(m.permissions(), std::vector<std::string>{"android.permission.INTERNET"});
}

TEST(ManifestVariants, ALoopInOneConfigurationIsUnknown)
{
    // e0 is false by default and refers to itself in v31: on Android 12 and
    // newer the value cannot be read.  dexpp counts that as possibly true.
    ArscBuilder b;
    b.value(0, ArscConfig(), ArscBuilder::kBoolean, 0);
    b.value(0, ArscConfig().sdk(31), ArscBuilder::kReference, ArscBuilder::id(0));
    Manifest m = crafted_with_table(literal_name(".A"),
                                    {reference_attr("exported", kExportedId, ArscBuilder::id(0))},
                                    b.build());
    EXPECT_EQ(only_component(m).exported, std::optional<bool>(false));
    EXPECT_TRUE(only_component(m).exported_varies);
    EXPECT_TRUE(only_component(m).is_exported());
}

TEST(ManifestVariants, AChainLongerThanAndroidFollowsIsUnknown)
{
    // By default e0 is false.  In v31 it starts a chain of 25 references:
    // Android gives up after 20, so that configuration has no value.
    ArscBuilder b;
    b.value(0, ArscConfig(), ArscBuilder::kBoolean, 0);
    b.value(0, ArscConfig().sdk(31), ArscBuilder::kReference, ArscBuilder::id(1));
    for (uint16_t i = 1; i < 25; ++i)
        b.value(i, ArscConfig(), ArscBuilder::kReference,
                ArscBuilder::id(static_cast<uint16_t>(i + 1)));
    b.value(25, ArscConfig(), ArscBuilder::kBoolean, 0);
    Manifest m = crafted_with_table(literal_name(".A"),
                                    {reference_attr("exported", kExportedId, ArscBuilder::id(0))},
                                    b.build());
    EXPECT_EQ(only_component(m).exported, std::optional<bool>(false));
    EXPECT_TRUE(only_component(m).exported_varies);

    // The same chain, short enough: every configuration says false.
    ArscBuilder short_chain;
    short_chain.value(0, ArscConfig(), ArscBuilder::kBoolean, 0);
    short_chain.value(0, ArscConfig().sdk(31), ArscBuilder::kReference, ArscBuilder::id(1));
    for (uint16_t i = 1; i < 5; ++i)
        short_chain.value(i, ArscConfig(), ArscBuilder::kReference,
                          ArscBuilder::id(static_cast<uint16_t>(i + 1)));
    short_chain.value(5, ArscConfig(), ArscBuilder::kBoolean, 0);
    Manifest short_m = crafted_with_table(
        literal_name(".A"), {reference_attr("exported", kExportedId, ArscBuilder::id(0))},
        short_chain.build());
    EXPECT_EQ(only_component(short_m).exported, std::optional<bool>(false));
    EXPECT_FALSE(only_component(short_m).exported_varies);
    EXPECT_FALSE(only_component(short_m).is_exported());
}

TEST(ManifestVariants, TheSearchIsBounded)
{
    // A boolean with a value in 5000 configurations, all false.  The search
    // looks at no more than a few thousand values, so the rest is unknown:
    // the answer stays safe (possibly exported) and the work stays small.
    ArscBuilder b;
    for (uint16_t sdk = 1; sdk <= 5000; ++sdk)
        b.value(0, ArscConfig().sdk(sdk), ArscBuilder::kBoolean, 0);
    b.value(0, ArscConfig(), ArscBuilder::kBoolean, 0);
    Manifest m = crafted_with_table(literal_name(".A"),
                                    {reference_attr("exported", kExportedId, ArscBuilder::id(0))},
                                    b.build());
    EXPECT_EQ(only_component(m).exported, std::optional<bool>(false));
    EXPECT_TRUE(only_component(m).exported_varies);
    EXPECT_TRUE(only_component(m).is_exported());
}

// ===== null values =====
//
// Android reads a null value as if the attribute were not written: `@null`
// (a reference to id 0), `@empty` (TYPE_NULL, DATA_NULL_EMPTY), and a
// reference whose resource ends at one of them.  AttributeResolution.cpp turns
// `@null` into TYPE_NULL, TypedArray.hasValue() is false for TYPE_NULL, and
// getBoolean(), getInt() and getNonConfigurationString() give their default.

TEST(ManifestNullValues, AReferenceToANullValueReadsAsNotWritten)
{
    Apk apk = open_refs();
    const Manifest *m = apk.manifest();
    ASSERT_NE(m, nullptr);

    // @bool/nothing is @null.  Not written, so the intent filters decide:
    // there are none.  The reference id is kept.
    const Component *c = find_component(*m, "com.dexpp.refs.NullActivity");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->exported, std::nullopt);
    EXPECT_EQ(c->exported_reference, resource_id(apk, "bool", "nothing"));
    EXPECT_FALSE(c->exported_unknown);
    EXPECT_FALSE(c->exported_varies);
    EXPECT_FALSE(c->is_exported());
    EXPECT_FALSE(c->is_reachable());

    // @bool/same_as_nothing is @bool/nothing: the chain ends at @null.  With
    // an intent filter, the component is exported.
    c = find_component(*m, "com.dexpp.refs.NullFilteredActivity");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->exported, std::nullopt);
    EXPECT_EQ(c->exported_reference, resource_id(apk, "bool", "same_as_nothing"));
    EXPECT_FALSE(c->exported_unknown);
    EXPECT_TRUE(c->is_exported());

    // @bool/empty_bool is @empty: the same as @null for a boolean.
    c = find_component(*m, "com.dexpp.refs.EmptyActivity");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->exported, std::nullopt);
    EXPECT_EQ(c->exported_reference, resource_id(apk, "bool", "empty_bool"));
    EXPECT_FALSE(c->exported_unknown);
    EXPECT_FALSE(c->is_exported());

    // android:exported="@null" directly: a reference to id 0, which is no
    // resource, so it reads like a missing attribute.
    c = find_component(*m, "com.dexpp.refs.LiteralNullActivity");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->exported, std::nullopt);
    EXPECT_EQ(c->exported_reference, std::nullopt);
    EXPECT_FALSE(c->exported_unknown);
    EXPECT_TRUE(c->is_exported()); // it has an intent filter

    // android:permission="@string/no_permission", which is @null: no
    // permission, and that is known.
    c = find_component(*m, "com.dexpp.refs.NullPermissionService");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->permission, std::nullopt);
    EXPECT_EQ(c->permission_reference, resource_id(apk, "string", "no_permission"));
    EXPECT_FALSE(c->permission_unknown);
    EXPECT_FALSE(c->permission_inherited);
    EXPECT_TRUE(c->is_reachable());
}

namespace {

constexpr uint32_t kReadPermissionId = 0x01010007;
constexpr uint32_t kWritePermissionId = 0x01010008;
constexpr uint32_t kProtectionLevelId = 0x01010009;
constexpr uint8_t kTypeNull = 0x00;
constexpr uint8_t kTypeIntHex = 0x11;

TestAttr typed_attr(std::string name, uint32_t id, uint8_t type, uint32_t data)
{
    return TestAttr{kAndroidNs,   std::move(name), id,
                    std::nullopt, std::nullopt,    axml::TypedValue{type, data}};
}

TestElement intent_filter()
{
    return {"intent-filter",
            {},
            {{"action", {{kAndroidNs, "name", kNameId, "android.intent.action.VIEW"}}, {}}}};
}

/** A manifest that declares com.test.SIG (signature) and com.test.NORMAL
 *  (normal), with an `<application>` that has @p app_attrs and
 *  @p components, read with the resource table @p table_bytes. */
Manifest app_manifest(std::vector<TestAttr> app_attrs, std::vector<TestElement> components,
                      const std::vector<uint8_t> &table_bytes)
{
    TestElement root{"manifest", {{"", "package", 0, "com.test"}}, {}};
    root.children.push_back({"permission",
                             {literal_name("com.test.SIG"),
                              typed_attr("protectionLevel", kProtectionLevelId, kTypeIntHex, 2)},
                             {}});
    root.children.push_back({"permission",
                             {literal_name("com.test.NORMAL"),
                              typed_attr("protectionLevel", kProtectionLevelId, kTypeIntHex, 0)},
                             {}});
    root.children.push_back({"application", std::move(app_attrs), std::move(components)});
    auto doc = axml::parse(AxmlWriter().write(root, ResourceMap::BeforeTheElements));
    EXPECT_TRUE(doc.has_value());
    auto table = ResourceTable::parse(table_bytes);
    EXPECT_TRUE(table.has_value());
    return Manifest(doc ? std::move(*doc) : axml::XmlDocument{}, table ? &*table : nullptr);
}

/** An exported component of kind @p tag named @p name with @p attrs. */
TestElement exported_component(std::string tag, std::string name, std::vector<TestAttr> attrs = {})
{
    TestElement e{std::move(tag),
                  {literal_name(std::move(name)),
                   TestAttr{kAndroidNs, "exported", kExportedId, std::nullopt, true}},
                  {}};
    for (TestAttr &a : attrs)
        e.attrs.push_back(std::move(a));
    return e;
}

const Component &named(const Manifest &m, std::string_view name)
{
    const Component *c = find_component(m, name);
    EXPECT_NE(c, nullptr) << name;
    static const Component missing{};
    return c ? *c : missing;
}

} // namespace

TEST(ManifestNullValues, LiteralNullValues)
{
    // TYPE_NULL with DATA_NULL_UNDEFINED, `@empty` and `@null` written
    // directly in the manifest: not written, so the intent filter decides.
    const std::vector<std::pair<uint8_t, uint32_t>> cases = {
        {kTypeNull, 0}, {kTypeNull, 1}, {kTypeReference, 0}};
    for (const auto &[type, data] : cases) {
        Manifest with_filter =
            one_activity({typed_attr("exported", kExportedId, type, data)},
                         /*with_intent_filter=*/true, ResourceMap::BeforeTheElements);
        const Component &c = only_component(with_filter);
        EXPECT_EQ(c.exported, std::nullopt) << int(type) << "/" << data;
        EXPECT_EQ(c.exported_reference, std::nullopt) << int(type) << "/" << data;
        EXPECT_FALSE(c.exported_unknown) << int(type) << "/" << data;
        EXPECT_TRUE(c.is_exported()) << int(type) << "/" << data;

        Manifest without =
            one_activity({typed_attr("exported", kExportedId, type, data)},
                         /*with_intent_filter=*/false, ResourceMap::BeforeTheElements);
        EXPECT_FALSE(only_component(without).is_exported()) << int(type) << "/" << data;
    }
}

TEST(ManifestNullValues, ANullValueInTheTable)
{
    // aapt2 writes @null in a table as a reference to 0; TYPE_NULL with
    // DATA_NULL_UNDEFINED or DATA_NULL_EMPTY is read the same way.
    ArscBuilder b;
    b.value(0, ArscConfig(), ArscBuilder::kNull, 0);
    b.value(1, ArscConfig(), ArscBuilder::kNull, 1);
    b.value(2, ArscConfig(), ArscBuilder::kReference, 0);
    const std::vector<uint8_t> table = b.build();
    for (uint16_t entry = 0; entry < 3; ++entry) {
        Manifest m = crafted_with_table(
            literal_name(".A"), {reference_attr("exported", kExportedId, ArscBuilder::id(entry))},
            table);
        const Component &c = only_component(m);
        EXPECT_EQ(c.exported, std::nullopt) << entry;
        EXPECT_EQ(c.exported_reference, ArscBuilder::id(entry)) << entry;
        EXPECT_FALSE(c.exported_unknown) << entry;
        EXPECT_FALSE(c.is_exported()) << entry;
    }
}

TEST(ManifestNullValues, NullInOneConfigurationUsesTheIntentFilters)
{
    // e0 is false by default and @null in values-v31: on Android 12 and
    // newer the attribute counts as not written, so the intent filters
    // decide there.
    ArscBuilder b;
    b.value(0, ArscConfig(), ArscBuilder::kBoolean, 0);
    b.value(0, ArscConfig().sdk(31), ArscBuilder::kReference, 0);
    const std::vector<uint8_t> table = b.build();
    const TestAttr exported = reference_attr("exported", kExportedId, ArscBuilder::id(0));

    // No intent filter: not exported in either configuration.
    Manifest closed = app_manifest({}, {{"activity", {literal_name(".A"), exported}, {}}}, table);
    EXPECT_EQ(only_component(closed).exported, std::optional<bool>(false));
    EXPECT_FALSE(only_component(closed).exported_varies);
    EXPECT_FALSE(only_component(closed).is_exported());

    // An intent filter: exported on Android 12 and newer.
    Manifest open =
        app_manifest({}, {{"activity", {literal_name(".A"), exported}, {intent_filter()}}}, table);
    EXPECT_EQ(only_component(open).exported, std::optional<bool>(false));
    EXPECT_TRUE(only_component(open).exported_varies);
    EXPECT_TRUE(only_component(open).is_exported());
}

TEST(ManifestNullValues, ApplicationFlagsAndProtectionLevels)
{
    // e0 is @null.  debuggable and allowBackup read as not written (Android
    // uses their defaults), and a protection level reads as normal.  e1 is
    // @null by default and true in values-v31: debuggable then varies (its
    // default is false), allowBackup does not (its default is true).
    ArscBuilder b;
    b.value(0, ArscConfig(), ArscBuilder::kReference, 0);
    b.value(1, ArscConfig(), ArscBuilder::kReference, 0);
    b.value(1, ArscConfig().sdk(31), ArscBuilder::kBoolean, 0xFFFFFFFF);
    const std::vector<uint8_t> table = b.build();

    Manifest m = app_manifest({reference_attr("debuggable", kDebuggableId, ArscBuilder::id(0)),
                               reference_attr("allowBackup", kAllowBackupId, ArscBuilder::id(0))},
                              {}, table);
    EXPECT_EQ(m.debuggable(), std::nullopt);
    EXPECT_EQ(m.debuggable_reference(), ArscBuilder::id(0));
    EXPECT_FALSE(m.debuggable_unknown());
    EXPECT_FALSE(m.debuggable_varies());
    EXPECT_EQ(m.allow_backup(), std::nullopt);
    EXPECT_FALSE(m.allow_backup_unknown());

    Manifest varying =
        app_manifest({reference_attr("debuggable", kDebuggableId, ArscBuilder::id(1)),
                      reference_attr("allowBackup", kAllowBackupId, ArscBuilder::id(1))},
                     {}, table);
    EXPECT_EQ(varying.debuggable(), std::nullopt);
    EXPECT_TRUE(varying.debuggable_varies());
    EXPECT_FALSE(varying.allow_backup_varies());

    // <permission android:protectionLevel="@integer/e0">, e0 being @null.
    TestElement root{"manifest", {{"", "package", 0, "com.test"}}, {}};
    root.children.push_back(
        {"permission",
         {literal_name("com.test.P"),
          reference_attr("protectionLevel", kProtectionLevelId, ArscBuilder::id(0))},
         {}});
    auto doc = axml::parse(AxmlWriter().write(root, ResourceMap::BeforeTheElements));
    ASSERT_TRUE(doc.has_value());
    auto parsed = ResourceTable::parse(table);
    ASSERT_TRUE(parsed.has_value());
    Manifest levels(std::move(*doc), &*parsed);
    EXPECT_EQ(levels.declared_permission_protection_level("com.test.P"), 0);
    EXPECT_FALSE(levels.declared_permission_protection_level_varies("com.test.P"));
}

// ===== the application's permission =====
//
// ParsingPackageUtils reads <application android:permission> with
// getNonConfigurationString(attr, 0).  ParsedActivityUtils (activities and
// receivers) and ParsedServiceUtils give a component the application's
// permission when its own getNonConfigurationString returns null.
// ParsedProviderUtils does the same for a provider's read and write
// permissions, after android:readPermission / writePermission and
// android:permission.

TEST(ManifestApplicationPermission, EveryKindInherits)
{
    Manifest m =
        app_manifest({string_attr("permission", kPermissionId, "com.test.SIG")},
                     {exported_component("activity", ".A"), exported_component("service", ".S"),
                      exported_component("receiver", ".R"), exported_component("provider", ".P")},
                     ArscBuilder().value(0, ArscConfig(), ArscBuilder::kBoolean, 0).build());
    EXPECT_EQ(m.application_permission(), std::optional<std::string_view>("com.test.SIG"));
    ASSERT_EQ(m.components().size(), 4u);
    for (const Component &c : m.components()) {
        EXPECT_EQ(c.permission, std::optional<std::string>("com.test.SIG")) << c.name;
        EXPECT_TRUE(c.permission_inherited) << c.name;
        EXPECT_TRUE(c.guarded_by_signature_permission) << c.name;
        EXPECT_FALSE(c.is_reachable()) << c.name;
    }
    const Component &provider = named(m, "com.test.P");
    EXPECT_EQ(provider.read_permission, std::optional<std::string>("com.test.SIG"));
    EXPECT_EQ(provider.write_permission, std::optional<std::string>("com.test.SIG"));
}

TEST(ManifestApplicationPermission, AComponentPermissionThatGivesNothingInherits)
{
    // e0 is @null; e1 changes with the locale, so Android ignores it; e2 is
    // missing, so its value is unknown.
    ArscBuilder b;
    b.value(0, ArscConfig(), ArscBuilder::kReference, 0);
    const uint32_t normal = b.string("com.test.NORMAL");
    b.value(1, ArscConfig(), ArscBuilder::kString, normal);
    b.value(1, ArscConfig().language('f', 'r'), ArscBuilder::kString, normal);
    b.spec_flags(1, 0x0004);
    const uint32_t missing = ArscBuilder::id(2);
    Manifest m = app_manifest(
        {string_attr("permission", kPermissionId, "com.test.SIG")},
        {exported_component("activity", ".Null",
                            {reference_attr("permission", kPermissionId, ArscBuilder::id(0))}),
         exported_component("activity", ".Localized",
                            {reference_attr("permission", kPermissionId, ArscBuilder::id(1))}),
         exported_component("activity", ".Missing",
                            {reference_attr("permission", kPermissionId, missing)}),
         exported_component("activity", ".Empty",
                            {typed_attr("permission", kPermissionId, kTypeNull, 1)})},
        b.build());

    for (const char *name : {"com.test.Null", "com.test.Localized", "com.test.Empty"}) {
        const Component &c = named(m, name);
        EXPECT_EQ(c.permission, std::optional<std::string>("com.test.SIG")) << name;
        EXPECT_TRUE(c.permission_inherited) << name;
        EXPECT_FALSE(c.permission_unknown) << name;
        EXPECT_TRUE(c.guarded_by_signature_permission) << name;
        EXPECT_FALSE(c.is_reachable()) << name;
    }
    EXPECT_EQ(named(m, "com.test.Null").permission_reference, ArscBuilder::id(0));
    EXPECT_EQ(named(m, "com.test.Empty").permission_reference, std::nullopt);

    // The component's own value is unknown: it may be any permission, so the
    // application's does not count and the component is not guarded.
    const Component &unknown = named(m, "com.test.Missing");
    EXPECT_EQ(unknown.permission, std::nullopt);
    EXPECT_EQ(unknown.permission_reference, missing);
    EXPECT_TRUE(unknown.permission_unknown);
    EXPECT_FALSE(unknown.permission_inherited);
    EXPECT_FALSE(unknown.guarded_by_signature_permission);
    EXPECT_TRUE(unknown.is_reachable());
}

TEST(ManifestApplicationPermission, GivenByAReference)
{
    // e0 is com.test.SIG; e1 changes with the locale; e2 is missing.
    ArscBuilder b;
    const uint32_t sig = b.string("com.test.SIG");
    b.value(0, ArscConfig(), ArscBuilder::kString, sig);
    b.value(1, ArscConfig(), ArscBuilder::kString, sig);
    b.value(1, ArscConfig().language('f', 'r'), ArscBuilder::kString, sig);
    b.spec_flags(1, 0x0004);
    const std::vector<uint8_t> table = b.build();
    auto with_app_permission = [&](uint32_t id) {
        return app_manifest(
            {reference_attr("permission", kPermissionId, id)},
            {exported_component("activity", ".A"),
             exported_component("activity", ".Own",
                                {string_attr("permission", kPermissionId, "com.test.NORMAL")})},
            table);
    };

    Manifest resolved = with_app_permission(ArscBuilder::id(0));
    EXPECT_EQ(resolved.application_permission(), std::optional<std::string_view>("com.test.SIG"));
    EXPECT_EQ(resolved.application_permission_reference(), ArscBuilder::id(0));
    EXPECT_FALSE(resolved.application_permission_unknown());
    EXPECT_TRUE(named(resolved, "com.test.A").permission_inherited);
    EXPECT_FALSE(named(resolved, "com.test.A").is_reachable());
    // Its own normal permission wins.
    EXPECT_EQ(named(resolved, "com.test.Own").permission,
              std::optional<std::string>("com.test.NORMAL"));
    EXPECT_FALSE(named(resolved, "com.test.Own").permission_inherited);
    EXPECT_TRUE(named(resolved, "com.test.Own").is_reachable());

    // Android ignores a string that changes with the locale: no permission
    // to inherit.
    Manifest ignored = with_app_permission(ArscBuilder::id(1));
    EXPECT_EQ(ignored.application_permission(), std::nullopt);
    EXPECT_EQ(ignored.application_permission_reference(), ArscBuilder::id(1));
    EXPECT_FALSE(ignored.application_permission_unknown());
    EXPECT_EQ(named(ignored, "com.test.A").permission, std::nullopt);
    EXPECT_FALSE(named(ignored, "com.test.A").permission_inherited);
    EXPECT_TRUE(named(ignored, "com.test.A").is_reachable());

    // Unknown: the component inherits an unknown permission, which does not
    // guard it.
    Manifest unknown = with_app_permission(ArscBuilder::id(2));
    EXPECT_EQ(unknown.application_permission(), std::nullopt);
    EXPECT_TRUE(unknown.application_permission_unknown());
    const Component &a = named(unknown, "com.test.A");
    EXPECT_EQ(a.permission, std::nullopt);
    EXPECT_TRUE(a.permission_inherited);
    EXPECT_TRUE(a.permission_unknown);
    EXPECT_TRUE(a.is_reachable());
}

TEST(ManifestApplicationPermission, ProviderReadAndWritePermissions)
{
    // e0 is @null.
    ArscBuilder b;
    b.value(0, ArscConfig(), ArscBuilder::kReference, 0);
    const std::vector<uint8_t> table = b.build();
    Manifest m = app_manifest(
        {string_attr("permission", kPermissionId, "com.test.SIG")},
        {// readPermission is @null: it falls back to the application's.
         exported_component(
             "provider", ".NullRead",
             {reference_attr("readPermission", kReadPermissionId, ArscBuilder::id(0))}),
         // Its own permission is normal; readPermission is the signature one.
         exported_component("provider", ".OwnPermission",
                            {string_attr("permission", kPermissionId, "com.test.NORMAL"),
                             string_attr("readPermission", kReadPermissionId, "com.test.SIG")}),
         // Both given, both signature.
         exported_component("provider", ".Both",
                            {string_attr("readPermission", kReadPermissionId, "com.test.SIG"),
                             string_attr("writePermission", kWritePermissionId, "com.test.SIG")}),
         // A missing id: unknown.
         exported_component(
             "provider", ".UnknownWrite",
             {reference_attr("writePermission", kWritePermissionId, ArscBuilder::id(5))})},
        table);

    const Component &null_read = named(m, "com.test.NullRead");
    EXPECT_EQ(null_read.read_permission, std::optional<std::string>("com.test.SIG"));
    EXPECT_EQ(null_read.read_permission_reference, ArscBuilder::id(0));
    EXPECT_FALSE(null_read.read_permission_unknown);
    EXPECT_TRUE(null_read.guarded_by_signature_permission);

    const Component &own = named(m, "com.test.OwnPermission");
    EXPECT_EQ(own.permission, std::optional<std::string>("com.test.NORMAL"));
    EXPECT_FALSE(own.permission_inherited);
    EXPECT_EQ(own.read_permission, std::optional<std::string>("com.test.SIG"));
    EXPECT_EQ(own.write_permission, std::optional<std::string>("com.test.NORMAL"));
    EXPECT_FALSE(own.guarded_by_signature_permission);
    EXPECT_TRUE(own.is_reachable());

    EXPECT_TRUE(named(m, "com.test.Both").guarded_by_signature_permission);

    const Component &unknown = named(m, "com.test.UnknownWrite");
    EXPECT_EQ(unknown.read_permission, std::optional<std::string>("com.test.SIG"));
    EXPECT_EQ(unknown.write_permission, std::nullopt);
    EXPECT_TRUE(unknown.write_permission_unknown);
    EXPECT_FALSE(unknown.guarded_by_signature_permission);
    EXPECT_TRUE(unknown.is_reachable());
}
