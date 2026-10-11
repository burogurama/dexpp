// Tests for the resources.arsc decoder (ResourceTable) and Apk's resource
// resolution, against tests/data/sample.apk.
//
// sample.apk's res/values/strings.xml defines:
//   <string name="app_name">Dexpp Sample</string>
//   <string name="greeting">Hello from resources</string>
// and AndroidManifest.xml sets android:label="@string/app_name".

#include <gtest/gtest.h>

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
