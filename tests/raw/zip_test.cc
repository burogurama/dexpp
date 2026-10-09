// Tests for the minimal ZIP reader (raw/zip.hpp): listing, extraction, and the
// checks that keep a crafted archive from causing a bad read or a huge
// allocation.  Archives are built in memory by zip_builder.hpp; app.apk covers
// real (Huffman-coded) DEFLATE data.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "raw/zip.hpp"
#include "test_utils.hpp"
#include "zip_builder.hpp"

using namespace dex::raw;

namespace {

std::vector<uint8_t> bytes_of(const std::string &s) { return {s.begin(), s.end()}; }

// Deterministic, non-repeating-looking data large enough to need several
// stored DEFLATE blocks (each holds at most 65535 bytes).
std::vector<uint8_t> big_data()
{
    std::vector<uint8_t> data(200000);
    uint32_t x = 12345;
    for (auto &b : data) {
        x = x * 1103515245 + 12345;
        b = static_cast<uint8_t>(x >> 16);
    }
    return data;
}

} // namespace

TEST(ZipEntries, ListsEveryRecordInDirectoryOrder)
{
    auto zip =
        build_zip({stored_entry("b.txt", bytes_of("bee")), deflated_entry("a.txt", bytes_of("ay")),
                   stored_entry("b.txt", bytes_of("second"))});
    auto entries = zip::entries(zip);
    ASSERT_TRUE(entries.has_value()) << entries.error().message;
    ASSERT_EQ(entries->size(), 3u);
    EXPECT_EQ((*entries)[0].name, "b.txt");
    EXPECT_EQ((*entries)[0].method, 0);
    EXPECT_EQ((*entries)[0].uncompressed_size, 3u);
    EXPECT_EQ((*entries)[1].name, "a.txt");
    EXPECT_EQ((*entries)[1].method, 8);
    EXPECT_EQ((*entries)[2].name, "b.txt"); // duplicates are kept
}

TEST(ZipEntries, NonZipFails)
{
    std::vector<uint8_t> junk(100, 0xAB);
    EXPECT_FALSE(zip::entries(junk).has_value());
}

TEST(ZipReadEntry, ReadsStoredEntry)
{
    auto zip = build_zip({stored_entry("x", bytes_of("hello"))});
    auto out = zip::read_entry(zip, "x");
    ASSERT_TRUE(out.has_value()) << out.error().message;
    EXPECT_EQ(*out, bytes_of("hello"));
}

TEST(ZipReadEntry, ReadsDeflatedEntrySpanningSeveralBlocks)
{
    auto data = big_data();
    auto zip = build_zip({deflated_entry("big", data)});
    auto out = zip::read_entry(zip, "big");
    ASSERT_TRUE(out.has_value()) << out.error().message;
    EXPECT_EQ(*out, data);
}

TEST(ZipReadEntry, ReadsEmptyDeflatedEntry)
{
    auto zip = build_zip({deflated_entry("empty", {})});
    auto out = zip::read_entry(zip, "empty");
    ASSERT_TRUE(out.has_value()) << out.error().message;
    EXPECT_TRUE(out->empty());
}

TEST(ZipReadEntry, ReadsRealDeflateData)
{
    // app.apk's classes.dex is the classes.dex fixture, Huffman-compressed.
    auto apk = load_buf("tests/data/app.apk");
    auto out = zip::read_entry(apk, "classes.dex");
    ASSERT_TRUE(out.has_value()) << out.error().message;
    EXPECT_EQ(*out, load_buf("tests/data/classes.dex"));
}

TEST(ZipReadEntry, NameLookupReturnsFirstDuplicate)
{
    auto zip = build_zip(
        {stored_entry("dup", bytes_of("first")), stored_entry("dup", bytes_of("second"))});
    auto out = zip::read_entry(zip, "dup");
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(*out, bytes_of("first"));
}

TEST(ZipReadEntry, MissingNameFails)
{
    auto zip = build_zip({stored_entry("x", bytes_of("x"))});
    auto out = zip::read_entry(zip, "y");
    ASSERT_FALSE(out.has_value());
    EXPECT_NE(out.error().message.find("no entry named"), std::string::npos);
}

TEST(ZipReadEntry, TruncatedDeflateStreamFails)
{
    auto spec = deflated_entry("t", big_data());
    spec.payload.resize(spec.payload.size() / 2);
    auto zip = build_zip({spec});
    auto out = zip::read_entry(zip, "t");
    ASSERT_FALSE(out.has_value());
    EXPECT_NE(out.error().message.find("corrupt or size mismatch"), std::string::npos);
}

TEST(ZipReadEntry, StreamShorterThanDeclaredSizeFails)
{
    auto spec = deflated_entry("s", bytes_of("12345"));
    spec.uncompressed_size = 6;
    auto zip = build_zip({spec});
    auto out = zip::read_entry(zip, "s");
    ASSERT_FALSE(out.has_value());
    EXPECT_NE(out.error().message.find("corrupt or size mismatch"), std::string::npos);
}

TEST(ZipReadEntry, StreamLongerThanDeclaredSizeFails)
{
    auto spec = deflated_entry("l", bytes_of("12345"));
    spec.uncompressed_size = 4;
    auto zip = build_zip({spec});
    auto out = zip::read_entry(zip, "l");
    ASSERT_FALSE(out.has_value());
    EXPECT_NE(out.error().message.find("corrupt or size mismatch"), std::string::npos);
}

TEST(ZipReadEntry, ImplausibleDeclaredSizeIsRejectedBeforeAllocating)
{
    // ~10 bytes of stream claiming ~4 GB: no DEFLATE stream expands that much,
    // so this must fail up front instead of allocating the declared size.
    auto spec = deflated_entry("bomb", bytes_of("hi"));
    spec.uncompressed_size = 0xFFFFFFF0;
    auto zip = build_zip({spec});
    auto out = zip::read_entry(zip, "bomb");
    ASSERT_FALSE(out.has_value());
    EXPECT_NE(out.error().message.find("implausible"), std::string::npos);
}

TEST(ZipReadEntry, StoredEntryWithMismatchedSizesFails)
{
    auto spec = stored_entry("m", bytes_of("abc"));
    spec.uncompressed_size = 4;
    auto zip = build_zip({spec});
    auto out = zip::read_entry(zip, "m");
    ASSERT_FALSE(out.has_value());
    EXPECT_NE(out.error().message.find("mismatched sizes"), std::string::npos);
}

TEST(ZipReadEntry, UnsupportedMethodFails)
{
    ZipEntrySpec spec{"bz", 12, bytes_of("data"), 4}; // 12 = bzip2
    auto zip = build_zip({spec});
    auto out = zip::read_entry(zip, "bz");
    ASSERT_FALSE(out.has_value());
    EXPECT_NE(out.error().message.find("unsupported compression method 12"), std::string::npos);
}

TEST(ZipReadEntry, EntryOverloadMatchesNameLookup)
{
    auto data = big_data();
    auto zip = build_zip({stored_entry("a", bytes_of("aaa")), deflated_entry("b", data)});
    auto entries = zip::entries(zip);
    ASSERT_TRUE(entries.has_value());
    auto out = zip::read_entry(zip, (*entries)[1]);
    ASSERT_TRUE(out.has_value()) << out.error().message;
    EXPECT_EQ(*out, data);
}

TEST(ZipReadEntry, EntryOverloadRejectsOutOfRangeLocalHeader)
{
    auto zip = build_zip({stored_entry("a", bytes_of("aaa"))});
    // An offset near UINT32_MAX must not wrap the bounds check.
    zip::Entry bad{"a", 0, 3, 3, 0xFFFFFFF0};
    auto out = zip::read_entry(zip, bad);
    ASSERT_FALSE(out.has_value());
    EXPECT_NE(out.error().message.find("malformed local file header"), std::string::npos);
}

TEST(ZipReadEntry, EntryOverloadRejectsDataPastEndOfBuffer)
{
    auto zip = build_zip({stored_entry("a", bytes_of("aaa"))});
    auto entries = zip::entries(zip);
    ASSERT_TRUE(entries.has_value());
    zip::Entry bad = (*entries)[0];
    bad.compressed_size = bad.uncompressed_size = 0xFFFFFFF0;
    auto out = zip::read_entry(zip, bad);
    ASSERT_FALSE(out.has_value());
    EXPECT_NE(out.error().message.find("out of bounds"), std::string::npos);
}

TEST(ZipReadEntry, ConcurrentReadsAgreeWithSerial)
{
    // Each test runs in its own process, so these threads race through the
    // decompressor's first-use initialisation; the TSan job checks it is safe.
    auto data = big_data();
    auto zip = build_zip({deflated_entry("d", data)});
    auto entries = zip::entries(zip);
    ASSERT_TRUE(entries.has_value());

    constexpr int kThreads = 8;
    std::vector<std::vector<uint8_t>> outs(kThreads);
    {
        std::vector<std::jthread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&, t] {
                auto out = zip::read_entry(zip, (*entries)[0]);
                if (out.has_value())
                    outs[t] = std::move(*out);
            });
        }
    }
    for (const auto &out : outs)
        EXPECT_EQ(out, data);
}
