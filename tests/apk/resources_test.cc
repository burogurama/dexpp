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

#include "arsc_builder.hpp"
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

TEST(ResourcesTest, NotAResourceTableIsInvalidResources)
{
    // The manifest is binary XML, not a resource table.
    auto apk = Apk::open("tests/data/sample.apk");
    ASSERT_TRUE(apk.has_value());
    std::optional<std::vector<uint8_t>> manifest = apk->read("AndroidManifest.xml");
    ASSERT_TRUE(manifest.has_value());
    auto table = ResourceTable::parse(*manifest);
    ASSERT_FALSE(table.has_value());
    EXPECT_EQ(table.error().code, ErrorCode::InvalidResources);
    EXPECT_TRUE(table.error().problems.empty());
}

// ===== booleans and integers, against tests/data/refs.apk =====
//
// refs.apk's resources (sources: RefsValues.xml for res/values,
// RefsValuesV31.xml for res/values-v31, RefsValuesFr.xml for res/values-fr
// and RefsValuesSrLatnLandNight.xml for res/values-b+sr+Latn-land-night)
// hold booleans, integers, strings, a boolean that refers to another boolean,
// and values that differ between configurations.

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

TEST(ResourcesTest, StringsAreReadAsBooleansLikeAndroid)
{
    // TypedArray.getBoolean passes a string to XmlUtils.convertValueToBoolean:
    // exactly "1", "true" and "TRUE" are true, any other text is false, and
    // empty text gives the caller's default (no value here).
    RefsTable refs = open_refs();
    ASSERT_NE(refs.table, nullptr);
    EXPECT_EQ(refs.table->resolve_bool(refs.id("string", "not_a_bool")), std::optional<bool>(true));
    EXPECT_EQ(refs.table->resolve_bool(refs.id("string", "upper_true")), std::optional<bool>(true));
    EXPECT_EQ(refs.table->resolve_bool(refs.id("string", "one_text")), std::optional<bool>(true));
    EXPECT_EQ(refs.table->resolve_bool(refs.id("string", "capital_true")),
              std::optional<bool>(false));
    EXPECT_EQ(refs.table->resolve_bool(refs.id("string", "yes_text")), std::optional<bool>(false));
    EXPECT_EQ(refs.table->resolve_bool(refs.id("string", "not_a_number")),
              std::optional<bool>(false));
    EXPECT_EQ(refs.table->resolve_bool(refs.id("string", "empty_text")), std::nullopt);
}

TEST(ResourcesTest, AValueOfAnotherTypeIsNotResolved)
{
    RefsTable refs = open_refs();
    ASSERT_NE(refs.table, nullptr);
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

// ===== every configuration =====

TEST(ResourceValues, EveryConfigurationIsListed)
{
    RefsTable refs = open_refs();
    ASSERT_NE(refs.table, nullptr);

    std::vector<ResourceValue> values = refs.table->values(refs.id("bool", "differs_on_v31"));
    ASSERT_EQ(values.size(), 2u);
    EXPECT_EQ(values[0].configuration, "");
    EXPECT_EQ(values[0].type, 0x12);
    EXPECT_EQ(values[0].data, 0u);
    EXPECT_EQ(values[0].string, std::nullopt);
    EXPECT_EQ(values[0].as_bool(), std::optional<bool>(false));
    EXPECT_EQ(values[1].configuration, "v31");
    EXPECT_EQ(values[1].data, 0xFFFFFFFFu);
    EXPECT_EQ(values[1].as_bool(), std::optional<bool>(true));
    EXPECT_EQ(values[1].as_int(), std::optional<int64_t>(-1));

    // The names follow aapt2: a BCP 47 locale is written "b+sr+Latn", and
    // aapt2 adds v8 to a night configuration.
    values = refs.table->values(refs.id("string", "greeting"));
    ASSERT_EQ(values.size(), 3u);
    EXPECT_EQ(values[0].configuration, "");
    EXPECT_EQ(values[0].string, std::optional<std::string>("Hello"));
    EXPECT_EQ(values[1].configuration, "fr");
    EXPECT_EQ(values[1].string, std::optional<std::string>("Bonjour"));
    EXPECT_EQ(values[2].configuration, "b+sr+Latn-land-night-v8");
    EXPECT_EQ(values[2].string, std::optional<std::string>("Zdravo"));
    EXPECT_EQ(values[2].type, 0x03);
    EXPECT_EQ(values[2].as_int(), std::nullopt);
    EXPECT_EQ(values[2].as_bool(), std::optional<bool>(false));
}

TEST(ResourceValues, AReferenceIsNotFollowed)
{
    RefsTable refs = open_refs();
    ASSERT_NE(refs.table, nullptr);
    std::vector<ResourceValue> values = refs.table->values(refs.id("bool", "same_as_yes"));
    ASSERT_EQ(values.size(), 1u);
    EXPECT_EQ(values[0].type, 0x01);
    EXPECT_EQ(values[0].data, refs.id("bool", "yes"));
    EXPECT_EQ(values[0].as_bool(), std::nullopt);
    EXPECT_EQ(values[0].as_int(), std::nullopt);

    EXPECT_TRUE(refs.table->values(0x7f999999).empty());
}

TEST(ResourceValues, TheDefaultConfigurationIsUsedEvenWhenItComesLater)
{
    // The v31 value comes first in the table here; resolve_* still use the
    // default configuration's.
    test::ArscBuilder b;
    b.value(0, test::ArscConfig().sdk(31), test::ArscBuilder::kBoolean, 0xFFFFFFFF);
    b.value(0, test::ArscConfig(), test::ArscBuilder::kBoolean, 0);
    auto table = ResourceTable::parse(b.build());
    ASSERT_TRUE(table.has_value());
    EXPECT_EQ(table->resolve_bool(test::ArscBuilder::id(0)), std::optional<bool>(false));
    EXPECT_EQ(table->name_of(test::ArscBuilder::id(0))->entry, "e0");
    ASSERT_EQ(table->values(test::ArscBuilder::id(0)).size(), 2u);
}

TEST(ResourceValues, ConfigurationNames)
{
    using test::ArscConfig;
    const std::vector<std::pair<ArscConfig, std::string>> cases = {
        {ArscConfig().language('f', 'r').region('C', 'A'), "fr-rCA"},
        // A three-letter language, packed into two bytes: "fil".
        {ArscConfig().raw(8, 0xAD).raw(9, 0x05), "fil"},
        // Bytes that are not printable ASCII never reach the name.
        {ArscConfig().language('\x01', '\xff'), "??"},
        {ArscConfig().raw(12, 2).raw(14, 0x40).raw(15, 0x01), "land-xhdpi"},
        {ArscConfig().raw(30, 0x58).raw(31, 0x02).sdk(13), "sw600dp-v13"},
        {ArscConfig().raw(28, 0x80).raw(29, 0x26), "ldrtl-watch-night"},
        {ArscConfig().raw(4, 0x36).raw(5, 0x01).raw(18, 0x02).raw(17, 0x02),
         "mcc310-keyshidden-dpad"},
        {ArscConfig().sdk(34).raw(26, 1), "v34.1"},
    };
    test::ArscBuilder b;
    for (std::size_t i = 0; i < cases.size(); ++i)
        b.value(static_cast<uint16_t>(i), cases[i].first, test::ArscBuilder::kIntDec,
                static_cast<uint32_t>(i));
    auto table = ResourceTable::parse(b.build());
    ASSERT_TRUE(table.has_value());
    for (std::size_t i = 0; i < cases.size(); ++i) {
        std::vector<ResourceValue> values =
            table->values(test::ArscBuilder::id(static_cast<uint16_t>(i)));
        ASSERT_EQ(values.size(), 1u) << i;
        EXPECT_EQ(values[0].configuration, cases[i].second);
        EXPECT_EQ(values[0].as_int(), std::optional<int64_t>(static_cast<int64_t>(i)));
    }
}
