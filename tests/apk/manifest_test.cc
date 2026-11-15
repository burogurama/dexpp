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
            put32(out, a.text.has_value() ? plain_.at(*a.text) : no_entry);
            put16(out, 8); // Res_value size
            out.push_back(0);
            out.push_back(a.text.has_value() ? 0x03 : 0x12); // TYPE_STRING or TYPE_INT_BOOLEAN
            put32(out, a.text.has_value() ? plain_.at(*a.text)
                                          : (a.boolean.value_or(false) ? 0xFFFFFFFF : 0));
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
