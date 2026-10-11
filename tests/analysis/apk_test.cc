// Tests for AnalysisContext::from_apk.
//
// tests/data/app.apk ground truth (generated from existing DEX fixtures):
//   classes.dex   = classes.dex fixture (LTestDex;), deflated
//   classes2.dex  = fibonacci.dex fixture (LFibonacci;), stored
//   AndroidManifest.xml             — placeholder, must be ignored
//   res/values/decoy-classes3.dex   — not top-level, must be ignored
// tests/data/nodex.apk is a valid ZIP with no classes.dex.
// tests/data/multidex.apk: classes.dex .. classes4.dex = classes / fibonacci /
//   shapes / trycatch fixtures, deflated; classes5.dex = inherit fixture,
//   stored.  Four deflated entries is enough to take the parallel load path.

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "../raw/test_utils.hpp"
#include "../raw/zip_builder.hpp"
#include "dex.hpp"

using namespace dex;

namespace {

std::vector<std::string> class_names(const AnalysisContext &ctx)
{
    std::vector<std::string> names;
    for (const auto &c : ctx.classes())
        names.emplace_back(c.name());
    return names;
}

std::vector<std::string> all_strings(const AnalysisContext &ctx)
{
    std::vector<std::string> out;
    for (auto s : ctx.strings())
        out.emplace_back(s);
    return out;
}

// A deflated entry whose payload is not a DEX file: it decompresses fine but
// fails to parse (InvalidDexFile).
ZipEntrySpec not_a_dex(std::string name)
{
    std::string junk = "this is not a dex file, just some bytes";
    return deflated_entry(std::move(name), std::vector<uint8_t>(junk.begin(), junk.end()));
}

// A deflated entry whose stream is cut short: it fails to decompress (InvalidApk).
ZipEntrySpec corrupt_stream(std::string name)
{
    auto spec = deflated_entry(std::move(name), load_buf("tests/data/shapes.dex"));
    spec.payload.resize(spec.payload.size() / 2);
    return spec;
}

} // namespace

TEST(FromApkTest, LoadsMultidexInOrder)
{
    auto ctx = AnalysisContext::from_apk("tests/data/app.apk");
    ASSERT_TRUE(ctx.has_value()) << ctx.error().message;

    // Both a deflated and a stored DEX entry must load.
    auto test_dex = ctx->find_class("LTestDex;");
    ASSERT_TRUE(test_dex.has_value());
    auto fib = ctx->find_class("LFibonacci;");
    ASSERT_TRUE(fib.has_value());

    // classes.dex precedes classes2.dex: with one class per fixture, the
    // first class overall must come from classes.dex.
    auto classes = ctx->classes();
    ASSERT_GE(classes.size(), 2u);
    EXPECT_EQ(classes.front().name(), "LTestDex;");
}

TEST(FromApkTest, AnalysesWorkOnApkContext)
{
    auto ctx = AnalysisContext::from_apk("tests/data/app.apk");
    ASSERT_TRUE(ctx.has_value());

    const auto &g = ctx->call_graph();
    ASSERT_FALSE(g.empty());
    const CallNode *hello = g.find(MethodId{"LTestDex;", "helloDex", "()Ljava/lang/String;"});
    ASSERT_NE(hello, nullptr);
    EXPECT_TRUE(hello->resolved);

    EXPECT_FALSE(ctx->xrefs().string_refs("Hello Dex++!!").empty());
}

TEST(FromApkTest, MissingFileIsFileNotFound)
{
    auto ctx = AnalysisContext::from_apk("tests/data/nonexistent.apk");
    ASSERT_FALSE(ctx.has_value());
    EXPECT_EQ(ctx.error().code, AnalysisError::Code::FileNotFound);
}

TEST(FromApkTest, NonZipIsInvalidApk)
{
    auto ctx = AnalysisContext::from_apk("tests/data/classes.dex");
    ASSERT_FALSE(ctx.has_value());
    EXPECT_EQ(ctx.error().code, AnalysisError::Code::InvalidApk);
}

TEST(FromApkTest, ZipWithoutDexIsInvalidApk)
{
    auto ctx = AnalysisContext::from_apk("tests/data/nodex.apk");
    ASSERT_FALSE(ctx.has_value());
    EXPECT_EQ(ctx.error().code, AnalysisError::Code::InvalidApk);
    EXPECT_NE(ctx.error().message.find("no classes.dex"), std::string::npos);
}

TEST(FromApkTest, ParallelLoadMatchesSerialLoad)
{
    auto serial = AnalysisContext::from_apk("tests/data/multidex.apk", {.threads = 1});
    ASSERT_TRUE(serial.has_value()) << serial.error().message;
    auto parallel = AnalysisContext::from_apk("tests/data/multidex.apk", {.threads = 4});
    ASSERT_TRUE(parallel.has_value()) << parallel.error().message;
    auto automatic = AnalysisContext::from_apk("tests/data/multidex.apk");
    ASSERT_TRUE(automatic.has_value()) << automatic.error().message;

    auto expected = class_names(*serial);
    ASSERT_FALSE(expected.empty());
    EXPECT_EQ(expected.front(), "LTestDex;"); // classes.dex comes first
    EXPECT_EQ(class_names(*parallel), expected);
    EXPECT_EQ(class_names(*automatic), expected);
    EXPECT_EQ(all_strings(*parallel), all_strings(*serial));

    // Every DEX of the archive loaded, the stored one included.
    EXPECT_TRUE(parallel->find_class("LFibonacci;").has_value());
    EXPECT_TRUE(parallel->find_class("LTryCatch;").has_value());
}

TEST(FromApkTest, DuplicateEntryNameLoadsOnlyTheFirstEntry)
{
    auto zip = build_zip({stored_entry("classes.dex", load_buf("tests/data/classes.dex")),
                          stored_entry("classes.dex", load_buf("tests/data/fibonacci.dex"))});
    auto ctx = AnalysisContext::from_apk_buffer(zip);
    ASSERT_TRUE(ctx.has_value()) << ctx.error().message;
    EXPECT_EQ(class_names(*ctx), std::vector<std::string>{"LTestDex;"});
}

TEST(FromApkTest, ParallelLoadReportsFirstFailureInMultidexOrder)
{
    // classes2 and classes4 are both bad.  However the threads happen to run,
    // the error must name classes2.dex — the one the serial loop hits first.
    auto zip = build_zip({deflated_entry("classes.dex", load_buf("tests/data/classes.dex")),
                          not_a_dex("classes2.dex"),
                          deflated_entry("classes3.dex", load_buf("tests/data/shapes.dex")),
                          corrupt_stream("classes4.dex")});
    for (unsigned threads : {1u, 4u}) {
        auto ctx = AnalysisContext::from_apk_buffer(zip, "test.apk", {.threads = threads});
        ASSERT_FALSE(ctx.has_value());
        EXPECT_EQ(ctx.error().code, AnalysisError::Code::InvalidDexFile) << threads;
        EXPECT_EQ(ctx.error().message.rfind("test.apk: classes2.dex: ", 0), 0u)
            << threads << ": " << ctx.error().message;
    }
}

TEST(FromApkTest, ParallelLoadReportsDecompressionFailureAsInvalidApk)
{
    auto zip = build_zip({deflated_entry("classes.dex", load_buf("tests/data/classes.dex")),
                          deflated_entry("classes2.dex", load_buf("tests/data/fibonacci.dex")),
                          corrupt_stream("classes3.dex"), not_a_dex("classes4.dex")});
    for (unsigned threads : {1u, 4u}) {
        auto ctx = AnalysisContext::from_apk_buffer(zip, "test.apk", {.threads = threads});
        ASSERT_FALSE(ctx.has_value());
        EXPECT_EQ(ctx.error().code, AnalysisError::Code::InvalidApk) << threads;
        EXPECT_EQ(ctx.error().message.rfind("test.apk: classes3.dex: ", 0), 0u)
            << threads << ": " << ctx.error().message;
    }
}
