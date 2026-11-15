// Tests for the resources.arsc decoder (ResourceTable) and Apk's resource
// resolution, against tests/data/sample.apk and tests/data/refs.apk.
//
// sample.apk's res/values/strings.xml defines:
//   <string name="app_name">Dexpp Sample</string>
//   <string name="greeting">Hello from resources</string>
// and AndroidManifest.xml sets android:label="@string/app_name".

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "dex.hpp"

using namespace dex;
using namespace dex::apk;

namespace {

Apk open_sample()
{
    auto apk = Apk::open("tests/data/sample.apk");
    EXPECT_TRUE(apk.has_value());
    return std::move(*apk);
}

} // namespace

TEST(ResourcesTest, TablePresentAndNonEmpty)
{
    auto apk = open_sample();
    const ResourceTable *rt = apk.resources();
    ASSERT_NE(rt, nullptr);
    EXPECT_FALSE(rt->empty());
}

TEST(ResourcesTest, IdLookupAndStringResolution)
{
    auto apk = open_sample();
    const ResourceTable *rt = apk.resources();
    ASSERT_NE(rt, nullptr);

    auto id = rt->id_of("string", "app_name");
    ASSERT_TRUE(id.has_value());
    // Package id 0x7f, type "string", entry 0.
    EXPECT_EQ((*id >> 24) & 0xff, 0x7fu);

    auto value = rt->resolve_string(*id);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "Dexpp Sample");

    auto greeting_id = rt->id_of("string", "greeting");
    ASSERT_TRUE(greeting_id.has_value());
    EXPECT_EQ(rt->resolve_string(*greeting_id).value_or(""), "Hello from resources");
}

TEST(ResourcesTest, NameReconstruction)
{
    auto apk = open_sample();
    const ResourceTable *rt = apk.resources();
    ASSERT_NE(rt, nullptr);

    auto id = rt->id_of("string", "app_name");
    ASSERT_TRUE(id.has_value());

    auto name = rt->name_of(*id);
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(name->package, "com.example.dexpp");
    EXPECT_EQ(name->type, "string");
    EXPECT_EQ(name->entry, "app_name");
}

TEST(ResourcesTest, UnknownIdsReturnNullopt)
{
    auto apk = open_sample();
    const ResourceTable *rt = apk.resources();
    ASSERT_NE(rt, nullptr);

    EXPECT_FALSE(rt->name_of(0x7f999999).has_value());
    EXPECT_FALSE(rt->resolve_string(0x7f999999).has_value());
    EXPECT_FALSE(rt->id_of("string", "no_such_entry").has_value());
}

// End-to-end: a manifest attribute that is a resource reference resolves to
// its string value through the table.
TEST(ResourcesTest, ResolvesManifestLabelReference)
{
    auto apk = open_sample();
    const Manifest *m = apk.manifest();
    ASSERT_NE(m, nullptr);

    const axml::XmlNode *app = m->xml().root.child("application");
    ASSERT_NE(app, nullptr);
    const axml::XmlAttribute *label = app->attribute("label");
    ASSERT_NE(label, nullptr);

    auto ref = label->as_reference();
    ASSERT_TRUE(ref.has_value()) << "android:label should be a @string reference";

    auto resolved = apk.resolve_string(*ref);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(*resolved, "Dexpp Sample");
}

TEST(ResourcesTest, NoResourcesWhenAbsent)
{
    // A plain DEX has no APK around it; opening it as an APK already fails,
    // but a ZIP without resources.arsc must yield a null resource table.
    auto apk = Apk::open("tests/data/app.apk"); // multidex zip, no resources.arsc
    ASSERT_TRUE(apk.has_value());
    EXPECT_EQ(apk->resources(), nullptr);
    EXPECT_FALSE(apk->resolve_string(0x7f010000).has_value());
}

// ===== booleans and integers, against tests/data/refs.apk =====
//
// refs.apk's resources (sources: RefsValues.xml for res/values and
// RefsValuesV31.xml for res/values-v31) hold booleans, integers, strings, a
// boolean that refers to another boolean, and a boolean whose value differs
// in the v31 configuration.

namespace {

struct RefsTable
{
    Apk apk;
    const ResourceTable *table;

    /** The id of @p type / @p entry; fails the test when it is missing. */
    uint32_t id(std::string_view type, std::string_view entry) const
    {
        auto found = table->id_of(type, entry);
        EXPECT_TRUE(found.has_value()) << type << "/" << entry;
        return found.value_or(0);
    }
};

RefsTable open_refs()
{
    auto apk = Apk::open("tests/data/refs.apk");
    EXPECT_TRUE(apk.has_value());
    const ResourceTable *table = apk->resources();
    EXPECT_NE(table, nullptr);
    return RefsTable{std::move(*apk), table};
}

} // namespace

TEST(ResourcesTest, ResolvesBooleansAndIntegers)
{
    RefsTable refs = open_refs();
    ASSERT_NE(refs.table, nullptr);
    EXPECT_EQ(refs.table->resolve_bool(refs.id("bool", "yes")), std::optional<bool>(true));
    EXPECT_EQ(refs.table->resolve_bool(refs.id("bool", "no")), std::optional<bool>(false));
    EXPECT_EQ(refs.table->resolve_int(refs.id("integer", "min_sdk")), std::optional<int64_t>(23));
    EXPECT_EQ(refs.table->resolve_int(refs.id("integer", "signature_level")),
              std::optional<int64_t>(2));
}

TEST(ResourcesTest, FollowsAReferenceToAnotherResource)
{
    // <bool name="same_as_yes">@bool/yes</bool>
    RefsTable refs = open_refs();
    ASSERT_NE(refs.table, nullptr);
    EXPECT_EQ(refs.table->resolve_bool(refs.id("bool", "same_as_yes")), std::optional<bool>(true));
}

TEST(ResourcesTest, IntegerTypesConvertLikeAndroid)
{
    // TypedArray.getBoolean and getInt accept every integer-typed value.
    RefsTable refs = open_refs();
    ASSERT_NE(refs.table, nullptr);
    EXPECT_EQ(refs.table->resolve_bool(refs.id("integer", "one")), std::optional<bool>(true));
    // aapt2 writes true as 0xFFFFFFFF, which reads as -1.
    EXPECT_EQ(refs.table->resolve_int(refs.id("bool", "yes")), std::optional<int64_t>(-1));
    EXPECT_EQ(refs.table->resolve_int(refs.id("bool", "no")), std::optional<int64_t>(0));
}

TEST(ResourcesTest, AValueOfAnotherTypeIsNotResolved)
{
    RefsTable refs = open_refs();
    ASSERT_NE(refs.table, nullptr);
    // <string name="not_a_bool">true</string>: a string, not a boolean.
    EXPECT_EQ(refs.table->resolve_bool(refs.id("string", "not_a_bool")), std::nullopt);
    EXPECT_EQ(refs.table->resolve_int(refs.id("string", "not_a_number")), std::nullopt);
    EXPECT_EQ(refs.table->resolve_string(refs.id("bool", "yes")), std::nullopt);
    EXPECT_EQ(refs.table->resolve_string(refs.id("string", "not_a_bool")),
              std::optional<std::string_view>("true"));
}

TEST(ResourcesTest, UnknownIdsDoNotResolveToABooleanOrInteger)
{
    RefsTable refs = open_refs();
    ASSERT_NE(refs.table, nullptr);
    EXPECT_EQ(refs.table->resolve_bool(0x7f999999), std::nullopt);
    EXPECT_EQ(refs.table->resolve_int(0x7f999999), std::nullopt);
    EXPECT_EQ(refs.table->resolve_bool(0), std::nullopt);
}

TEST(ResourcesTest, TheDefaultConfigurationWins)
{
    // differs_on_v31 is false in res/values and true in res/values-v31.
    RefsTable refs = open_refs();
    ASSERT_NE(refs.table, nullptr);
    EXPECT_EQ(refs.table->resolve_bool(refs.id("bool", "differs_on_v31")),
              std::optional<bool>(false));
}

TEST(ResourcesTest, AReferenceLoopIsNotResolved)
{
    // Make same_as_yes refer to itself instead of to bool/yes.  The lookup
    // must give up, not loop forever.
    RefsTable refs = open_refs();
    ASSERT_NE(refs.table, nullptr);
    const uint32_t self = refs.id("bool", "same_as_yes");
    const uint32_t yes = refs.id("bool", "yes");

    auto arsc = refs.apk.read("resources.arsc");
    ASSERT_TRUE(arsc.has_value());
    // Res_value: size 8, res0 0, dataType 0x01 (reference), data = the id.
    auto value_bytes = [](uint32_t id) {
        return std::vector<uint8_t>{8,
                                    0,
                                    0,
                                    0x01,
                                    static_cast<uint8_t>(id),
                                    static_cast<uint8_t>(id >> 8),
                                    static_cast<uint8_t>(id >> 16),
                                    static_cast<uint8_t>(id >> 24)};
    };
    std::vector<uint8_t> from = value_bytes(yes);
    std::vector<uint8_t> to = value_bytes(self);
    auto at = std::search(arsc->begin(), arsc->end(), from.begin(), from.end());
    ASSERT_NE(at, arsc->end());
    std::copy(to.begin(), to.end(), at);

    auto table = ResourceTable::parse(*arsc);
    ASSERT_TRUE(table.has_value());
    EXPECT_EQ(table->resolve_bool(self), std::nullopt);
    EXPECT_EQ(table->resolve_int(self), std::nullopt);
    EXPECT_EQ(table->resolve_string(self), std::nullopt);
    EXPECT_EQ(table->resolve_bool(yes), std::optional<bool>(true));
}
