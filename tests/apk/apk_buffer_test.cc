// Tests for Apk::from_buffer (an APK from bytes in memory) and for the load
// options of Apk::analysis.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "../raw/test_utils.hpp"
#include "dex.hpp"

using namespace dex;
using namespace dex::apk;

namespace {

std::vector<std::string> class_names(const AnalysisContext &ctx)
{
    std::vector<std::string> names;
    for (const Class &cls : ctx.classes())
        names.emplace_back(cls.descriptor());
    return names;
}

/** Whether two contexts are one analysis: handles compare equal only when
 *  they come from the same analysis object. */
bool same_analysis(const AnalysisContext &a, const AnalysisContext &b)
{
    return a.classes().front() == b.classes().front();
}

} // namespace

TEST(ApkFromBuffer, EverythingMatchesOpen)
{
    std::expected<Apk, Error> from_file = Apk::open("tests/data/signed.apk");
    ASSERT_TRUE(from_file.has_value()) << from_file.error().message;
    std::expected<Apk, Error> apk = Apk::from_buffer(load_buf("tests/data/signed.apk"));
    ASSERT_TRUE(apk.has_value()) << apk.error().message;

    EXPECT_EQ(apk->entries(), from_file->entries());
    EXPECT_EQ(apk->read("classes.dex"), from_file->read("classes.dex"));
    EXPECT_FALSE(apk->read("missing").has_value());

    ASSERT_NE(apk->manifest(), nullptr);
    EXPECT_EQ(apk->manifest()->package(), "com.example.dexpp");
    EXPECT_EQ(apk->manifest()->components().size(), from_file->manifest()->components().size());

    ASSERT_NE(apk->resources(), nullptr);
    std::optional<uint32_t> app_name = apk->resources()->id_of("string", "app_name");
    ASSERT_TRUE(app_name.has_value());
    EXPECT_EQ(apk->resolve_string(*app_name), "Dexpp Sample");

    const SigningInfo &signing = apk->signing();
    EXPECT_TRUE(signing.is_signed());
    EXPECT_TRUE(signing.v1 && signing.v2 && signing.v3);
    ASSERT_EQ(signing.certificates.size(), from_file->signing().certificates.size());
    EXPECT_EQ(signing.certificates[0].sha256_hex, from_file->signing().certificates[0].sha256_hex);

    std::expected<AnalysisContext, Error> ctx = apk->analysis();
    ASSERT_TRUE(ctx.has_value()) << ctx.error().message;
    std::expected<AnalysisContext, Error> ctx_from_file = from_file->analysis();
    ASSERT_TRUE(ctx_from_file.has_value());
    EXPECT_EQ(class_names(*ctx), class_names(*ctx_from_file));
}

TEST(ApkFromBuffer, SpanOverloadCopiesTheBytes)
{
    std::vector<uint8_t> bytes = load_buf("tests/data/sample.apk");
    std::expected<Apk, Error> apk = Apk::from_buffer(std::span<const uint8_t>(bytes), "sample.apk");
    ASSERT_TRUE(apk.has_value()) << apk.error().message;
    // The caller's bytes may change or go away: the Apk has its own copy.
    std::fill(bytes.begin(), bytes.end(), uint8_t{0});
    bytes.clear();
    bytes.shrink_to_fit();
    ASSERT_NE(apk->manifest(), nullptr);
    EXPECT_EQ(apk->manifest()->package(), "com.example.dexpp");
    EXPECT_TRUE(apk->read("classes.dex").has_value());
}

TEST(ApkFromBuffer, LvalueVectorIsCopied)
{
    std::vector<uint8_t> bytes = load_buf("tests/data/sample.apk");
    const std::size_t size = bytes.size();
    std::expected<Apk, Error> apk = Apk::from_buffer(bytes);
    ASSERT_TRUE(apk.has_value()) << apk.error().message;
    EXPECT_EQ(bytes.size(), size); // not moved from
    std::fill(bytes.begin(), bytes.end(), uint8_t{0});
    EXPECT_TRUE(apk->read("classes.dex").has_value());
}

TEST(ApkFromBuffer, BytesThatAreNotAnApkAreInvalidApk)
{
    const std::vector<uint8_t> dex = load_buf("tests/data/classes.dex");
    std::vector<uint8_t> truncated = load_buf("tests/data/sample.apk");
    truncated.resize(truncated.size() / 2); // the end of central directory is gone
    struct Case
    {
        std::vector<uint8_t> bytes;
        std::string label;
    };
    for (const Case &c : {Case{dex, "classes.dex"}, Case{{}, "empty.apk"},
                          Case{truncated, "truncated.apk"}, Case{{'P', 'K'}, "<apk>"}}) {
        SCOPED_TRACE(c.label);
        std::expected<Apk, Error> apk = Apk::from_buffer(c.bytes, c.label);
        ASSERT_FALSE(apk.has_value());
        EXPECT_EQ(apk.error().code, ErrorCode::InvalidApk);
        EXPECT_TRUE(apk.error().problems.empty());
        EXPECT_EQ(apk.error().message.rfind(c.label + ": ", 0), 0u) << apk.error().message;
    }

    // The default label.
    std::expected<Apk, Error> unnamed = Apk::from_buffer(std::vector<uint8_t>{1, 2, 3});
    ASSERT_FALSE(unnamed.has_value());
    EXPECT_EQ(unnamed.error().message.rfind("<apk>: ", 0), 0u) << unnamed.error().message;

    // open() gives the same code and the same message, with the path as label.
    std::expected<Apk, Error> opened = Apk::open("tests/data/classes.dex");
    std::expected<Apk, Error> from_bytes = Apk::from_buffer(dex, "tests/data/classes.dex");
    ASSERT_FALSE(opened.has_value());
    ASSERT_FALSE(from_bytes.has_value());
    EXPECT_EQ(opened.error().code, from_bytes.error().code);
    EXPECT_EQ(opened.error().message, from_bytes.error().message);
}

TEST(ApkFromBuffer, LabelStartsTheAnalysisErrors)
{
    std::expected<Apk, Error> apk =
        Apk::from_buffer(load_buf("tests/data/nodex.apk"), "upload.apk");
    ASSERT_TRUE(apk.has_value()) << apk.error().message;
    std::expected<AnalysisContext, Error> ctx = apk->analysis();
    ASSERT_FALSE(ctx.has_value());
    EXPECT_EQ(ctx.error().code, ErrorCode::InvalidApk);
    EXPECT_EQ(ctx.error().message, "upload.apk: APK contains no classes.dex");

    // open() puts the path there.
    std::expected<Apk, Error> opened = Apk::open("tests/data/nodex.apk");
    ASSERT_TRUE(opened.has_value());
    EXPECT_EQ(opened->analysis().error().message,
              "tests/data/nodex.apk: APK contains no classes.dex");
}

TEST(ApkAnalysisOptions, OneAnalysisPerOptionsValue)
{
    // multidex.apk has four deflated DEX entries, enough for the parallel path.
    std::expected<Apk, Error> apk = Apk::open("tests/data/multidex.apk");
    ASSERT_TRUE(apk.has_value()) << apk.error().message;

    std::expected<AnalysisContext, Error> automatic = apk->analysis();
    ASSERT_TRUE(automatic.has_value()) << automatic.error().message;
    // The default is {} is {.threads = 0}: the same cached analysis.
    std::expected<AnalysisContext, Error> again = apk->analysis(ApkLoadOptions{});
    ASSERT_TRUE(again.has_value());
    EXPECT_TRUE(same_analysis(*automatic, *again));
    EXPECT_TRUE(same_analysis(*automatic, *apk->analysis({.threads = 0})));

    // Other options: a second analysis, built once and kept too.
    std::expected<AnalysisContext, Error> serial = apk->analysis({.threads = 1});
    ASSERT_TRUE(serial.has_value()) << serial.error().message;
    EXPECT_FALSE(same_analysis(*automatic, *serial));
    EXPECT_TRUE(same_analysis(*serial, *apk->analysis({.threads = 1})));
    EXPECT_TRUE(same_analysis(*automatic, *apk->analysis()));

    // The two give the same answers.
    EXPECT_EQ(class_names(*serial), class_names(*automatic));
    EXPECT_EQ(class_names(*serial).front(), "LTestDex;");
    EXPECT_EQ(serial->call_graph().nodes().size(), automatic->call_graph().nodes().size());
    EXPECT_EQ(serial->call_graph().edges().size(), automatic->call_graph().edges().size());
}

TEST(ApkAnalysisOptions, FailureIsTheSameForEveryOptionsValue)
{
    std::expected<Apk, Error> apk = Apk::open("tests/data/nodex.apk");
    ASSERT_TRUE(apk.has_value()) << apk.error().message;
    for (unsigned threads : {0u, 1u, 4u, 1u}) {
        std::expected<AnalysisContext, Error> ctx = apk->analysis({.threads = threads});
        ASSERT_FALSE(ctx.has_value()) << threads;
        EXPECT_EQ(ctx.error().code, ErrorCode::InvalidApk) << threads;
    }
}

TEST(ApkAnalysisOptions, CopiesOfAnApkShareTheCache)
{
    // Apk is a handle: a copy shares the bytes and the cached analyses.
    std::expected<Apk, Error> apk = Apk::open("tests/data/sample.apk");
    ASSERT_TRUE(apk.has_value()) << apk.error().message;
    Apk copy = *apk;
    std::expected<AnalysisContext, Error> a = apk->analysis({.threads = 1});
    std::expected<AnalysisContext, Error> b = copy.analysis({.threads = 1});
    ASSERT_TRUE(a.has_value() && b.has_value());
    EXPECT_TRUE(same_analysis(*a, *b));
}
