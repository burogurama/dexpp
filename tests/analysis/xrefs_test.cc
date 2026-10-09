#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "dex.hpp"

using namespace dex;

// ===== classes.dex ground truth =====
// TestDex.helloDex(): const-string "Hello Dex++!!" at offset 0
// TestDex.main():     sget-object System.out at 0, invoke-static helloDex at 2,
//                     invoke-virtual PrintStream.println at 6

TEST(XrefsTest, StringRefFoundWithReferrer)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value()) << ctx.error().message;
    const auto &x = ctx->xrefs();
    ASSERT_FALSE(x.empty());

    auto sites = x.string_refs("Hello Dex++!!");
    ASSERT_EQ(sites.size(), 1u);
    EXPECT_EQ(sites[0].code_offset, 0u);

    const MethodId &referrer = x.referrer_id(sites[0].referrer);
    EXPECT_EQ(referrer.class_descriptor, "LTestDex;");
    EXPECT_EQ(referrer.name, "helloDex");

    auto m = x.referrer_method(sites[0].referrer);
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->name(), "helloDex");
    EXPECT_TRUE(m->has_code());
}

TEST(XrefsTest, UnknownKeysReturnEmpty)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());
    const auto &x = ctx->xrefs();

    EXPECT_TRUE(x.string_refs("no such string").empty());
    EXPECT_TRUE(x.type_refs("LNoSuchClass;").empty());
    EXPECT_TRUE(x.field_refs(FieldId{"LNo;", "where", "I"}).empty());
    EXPECT_TRUE(x.method_refs(MethodId{"LNo;", "where", "()V"}).empty());
    EXPECT_TRUE(x.method_refs_by_name("noSuchMethod").empty());
}

TEST(XrefsTest, ReferencedStringsEnumerates)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());
    const auto &x = ctx->xrefs();

    auto strings = x.referenced_strings();
    EXPECT_NE(std::find(strings.begin(), strings.end(), "Hello Dex++!!"), strings.end());
}

TEST(XrefsTest, StaticFieldReadIndexed)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());
    const auto &x = ctx->xrefs();

    FieldId out{"Ljava/lang/System;", "out", "Ljava/io/PrintStream;"};
    auto sites = x.field_refs(out);
    ASSERT_EQ(sites.size(), 1u);
    EXPECT_TRUE(sites[0].is_static);
    EXPECT_FALSE(sites[0].is_write);
    EXPECT_EQ(sites[0].code_offset, 0u);
    EXPECT_EQ(x.referrer_id(sites[0].referrer).name, "main");

    EXPECT_EQ(x.field_reads(out).size(), 1u);
    EXPECT_TRUE(x.field_writes(out).empty());
}

TEST(XrefsTest, ExactMethodRefIndexed)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());
    const auto &x = ctx->xrefs();

    auto sites = x.method_refs(MethodId{"LTestDex;", "helloDex", "()Ljava/lang/String;"});
    ASSERT_EQ(sites.size(), 1u);
    EXPECT_EQ(sites[0].kind, InvokeKind::Static);
    EXPECT_EQ(sites[0].code_offset, 2u);
    EXPECT_EQ(x.referrer_id(sites[0].referrer).name, "main");
}

TEST(XrefsTest, MethodRefsByNameSingleTarget)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());
    const auto &x = ctx->xrefs();

    auto results = x.method_refs_by_name("println");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].first->class_descriptor, "Ljava/io/PrintStream;");
    ASSERT_EQ(results[0].second.size(), 1u);
    EXPECT_EQ(results[0].second[0].kind, InvokeKind::Virtual);
    EXPECT_EQ(x.referrer_id(results[0].second[0].referrer).name, "main");
}

// ===== polymorphic.dex ground truth =====
// Pet.<init>:    iput-object Pet.name at offset 3
// Pet.describe:  new-instance StringBuilder at 0, iget-object Pet.name at 5,
//                invoke-virtual sound() on LPet;
// Dog.describe:  new-instance StringBuilder, invoke-super Pet.describe
// Kennel.noise:  invoke-interface Animal.sound()
// Kennel.dogNoise: invoke-virtual Dog.sound()

TEST(XrefsTest, InstanceFieldReadsAndWritesSplit)
{
    auto ctx = AnalysisContext::from_dex("tests/data/polymorphic.dex");
    ASSERT_TRUE(ctx.has_value());
    const auto &x = ctx->xrefs();

    FieldId name{"LPet;", "name", "Ljava/lang/String;"};
    auto writes = x.field_writes(name);
    ASSERT_EQ(writes.size(), 1u);
    EXPECT_FALSE(writes[0].is_static);
    EXPECT_EQ(x.referrer_id(writes[0].referrer).name, "<init>");

    auto reads = x.field_reads(name);
    ASSERT_EQ(reads.size(), 1u);
    EXPECT_FALSE(reads[0].is_static);
    EXPECT_EQ(x.referrer_id(reads[0].referrer).name, "describe");

    EXPECT_EQ(x.field_refs(name).size(), 2u);
}

TEST(XrefsTest, MethodRefsByNameAggregatesDeclaredTargets)
{
    auto ctx = AnalysisContext::from_dex("tests/data/polymorphic.dex");
    ASSERT_TRUE(ctx.has_value());
    const auto &x = ctx->xrefs();

    // Three call sites name a method "sound", each through a different
    // declared target: Animal (interface), Pet (virtual), Dog (virtual).
    auto results = x.method_refs_by_name("sound");
    ASSERT_EQ(results.size(), 3u);

    std::vector<std::string> targets;
    std::size_t total_sites = 0;
    for (const auto &[id, sites] : results) {
        EXPECT_EQ(id->name, "sound");
        EXPECT_EQ(id->proto, "()Ljava/lang/String;");
        targets.push_back(id->class_descriptor);
        total_sites += sites.size();
    }
    std::sort(targets.begin(), targets.end());
    EXPECT_EQ(targets, (std::vector<std::string>{"LAnimal;", "LDog;", "LPet;"}));
    EXPECT_EQ(total_sites, 3u);
}

TEST(XrefsTest, TypeRefsNewInstance)
{
    auto ctx = AnalysisContext::from_dex("tests/data/polymorphic.dex");
    ASSERT_TRUE(ctx.has_value());
    const auto &x = ctx->xrefs();

    // String concatenation desugars to StringBuilder in both describe() methods.
    auto sites = x.type_refs("Ljava/lang/StringBuilder;");
    ASSERT_EQ(sites.size(), 2u);

    std::vector<std::string> referrer_classes;
    for (const auto &s : sites) {
        EXPECT_EQ(s.kind, TypeRefKind::NewInstance);
        EXPECT_EQ(x.referrer_id(s.referrer).name, "describe");
        referrer_classes.push_back(x.referrer_id(s.referrer).class_descriptor);
    }
    std::sort(referrer_classes.begin(), referrer_classes.end());
    EXPECT_EQ(referrer_classes, (std::vector<std::string>{"LDog;", "LPet;"}));
}

TEST(XrefsTest, ReturnsSameReferenceAcrossCalls)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());
    const auto &x1 = ctx->xrefs();
    const auto &x2 = ctx->xrefs();
    EXPECT_EQ(&x1, &x2) << "AnalysisContext::xrefs() must return cached reference";
}

TEST(XrefsTest, ReferrerIndicesInRange)
{
    auto ctx = AnalysisContext::from_dex("tests/data/polymorphic.dex");
    ASSERT_TRUE(ctx.has_value());
    const auto &x = ctx->xrefs();

    auto referrers = x.referrers();
    ASSERT_FALSE(referrers.empty());
    for (const auto &value : x.referenced_strings()) {
        for (const auto &s : x.string_refs(value))
            EXPECT_LT(s.referrer, referrers.size());
    }
}
