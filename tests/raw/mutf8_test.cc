// Lossless DEX string decoding and encoding (raw/mutf8.hpp).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "raw/mutf8.hpp"

using namespace dex::raw;

namespace {

// A string_data_item: uleb128 utf16 length, the bytes, a 0 terminator.
std::vector<uint8_t> item(uint32_t utf16_size, std::vector<uint8_t> bytes)
{
    std::vector<uint8_t> out;
    do {
        uint8_t b = utf16_size & 0x7F;
        utf16_size >>= 7;
        out.push_back(utf16_size ? (b | 0x80) : b);
    } while (utf16_size);
    out.insert(out.end(), bytes.begin(), bytes.end());
    out.push_back(0);
    return out;
}

std::string decode(uint32_t utf16_size, std::vector<uint8_t> bytes)
{
    auto buf = item(utf16_size, std::move(bytes));
    auto r = mutf8::decode_string_data(buf, 0);
    EXPECT_TRUE(r.has_value()) << (r ? "" : r.error().message);
    return r ? *r : std::string("<error>");
}

bool rejects(uint32_t utf16_size, std::vector<uint8_t> bytes)
{
    auto buf = item(utf16_size, std::move(bytes));
    return !mutf8::decode_string_data(buf, 0).has_value();
}

std::vector<uint8_t> encode(std::string_view s)
{
    std::vector<uint8_t> out;
    mutf8::append_mutf8(s, out);
    return out;
}

} // namespace

TEST(Mutf8Decode, AsciiAndMultiByte)
{
    EXPECT_EQ(decode(3, {'a', 'b', 'c'}), "abc");
    EXPECT_EQ(decode(1, {0xC3, 0xA9}), "\xC3\xA9");           // é
    EXPECT_EQ(decode(1, {0xE2, 0x82, 0xAC}), "\xE2\x82\xAC"); // €
    EXPECT_EQ(decode(0, {}), "");
}

TEST(Mutf8Decode, NulIsTwoBytesInTheFileAndOneInTheString)
{
    EXPECT_EQ(decode(3, {'a', 0xC0, 0x80, 'b'}), std::string("a\0b", 3));
}

TEST(Mutf8Decode, SurrogatePairBecomesOneFourByteCharacter)
{
    // U+1F600 is the pair D83D DE00.
    EXPECT_EQ(decode(2, {0xED, 0xA0, 0xBD, 0xED, 0xB8, 0x80}), "\xF0\x9F\x98\x80");
}

TEST(Mutf8Decode, UnpairedSurrogatesAreKept)
{
    // A lone high, a lone low, and a high followed by a non-surrogate: kept as
    // 3-byte sequences instead of being replaced by U+FFFD.
    EXPECT_EQ(decode(2, {'a', 0xED, 0xA0, 0x80}), "a\xED\xA0\x80");
    EXPECT_EQ(decode(1, {0xED, 0xB0, 0x80}), "\xED\xB0\x80");
    EXPECT_EQ(decode(2, {0xED, 0xA0, 0x80, 'x'}), "\xED\xA0\x80x");
    // These differ from each other and from U+FFFD.
    EXPECT_NE(decode(1, {0xED, 0xA0, 0x80}), decode(1, {0xEF, 0xBF, 0xBD}));
}

TEST(Mutf8Decode, RejectsWhatArtRejects)
{
    EXPECT_TRUE(rejects(1, {0x80}));                   // stray continuation byte
    EXPECT_TRUE(rejects(1, {0xF0, 0x9F, 0x98, 0x80})); // 4-byte sequence
    EXPECT_TRUE(rejects(1, {0xC0, 0x81}));             // overlong (only C0 80 is allowed)
    EXPECT_TRUE(rejects(1, {0xE0, 0x80, 0x80}));       // overlong 3-byte
    EXPECT_TRUE(rejects(1, {0xC3, 0x41}));             // bad continuation
    EXPECT_TRUE(rejects(2, {'a'}));                    // shorter than declared
    EXPECT_TRUE(rejects(1, {'a', 'b'}));               // longer than declared
    std::vector<uint8_t> cut = {0x02, 'a'};            // no terminator
    EXPECT_FALSE(mutf8::decode_string_data(cut, 0).has_value());
}

TEST(Mutf8Encode, InverseOfDecode)
{
    const std::vector<std::string> samples = {
        "",
        "plain",
        std::string("a\0b", 3),
        "\xC3\xA9",
        "\xE2\x82\xAC",
        "\xF0\x9F\x98\x80",
        "a\xED\xA0\x80",
        "\xED\xB0\x80z",
    };
    for (const std::string &s : samples) {
        auto bytes = encode(s);
        auto buf = item(mutf8::utf16_length(s), bytes);
        auto back = mutf8::decode_string_data(buf, 0);
        ASSERT_TRUE(back.has_value()) << s;
        EXPECT_EQ(*back, s);
    }
    EXPECT_EQ(encode(std::string("\0", 1)), (std::vector<uint8_t>{0xC0, 0x80}));
    EXPECT_EQ(encode("\xF0\x9F\x98\x80"),
              (std::vector<uint8_t>{0xED, 0xA0, 0xBD, 0xED, 0xB8, 0x80}));
}

TEST(Mutf8, Utf16Length)
{
    EXPECT_EQ(mutf8::utf16_length("abc"), 3u);
    EXPECT_EQ(mutf8::utf16_length("\xF0\x9F\x98\x80"), 2u);
    EXPECT_EQ(mutf8::utf16_length("\xED\xA0\x80"), 1u);
    EXPECT_EQ(mutf8::utf16_length(std::string("\0", 1)), 1u);
}

TEST(Mutf8, Wtf8Validity)
{
    EXPECT_TRUE(mutf8::is_valid_wtf8("abc"));
    EXPECT_TRUE(mutf8::is_valid_wtf8("\xED\xA0\x80"));      // lone surrogate: allowed
    EXPECT_TRUE(mutf8::is_valid_wtf8("\xF4\x8F\xBF\xBF"));  // U+10FFFF
    EXPECT_FALSE(mutf8::is_valid_wtf8("\xC0\x80"));         // overlong
    EXPECT_FALSE(mutf8::is_valid_wtf8("\xF4\x90\x80\x80")); // above U+10FFFF
    EXPECT_FALSE(mutf8::is_valid_wtf8("\xC3"));             // cut off
    EXPECT_FALSE(mutf8::is_valid_wtf8("\xFF"));
}

TEST(Mutf8, CanonicalFormJoinsSurrogatePairs)
{
    const std::string pair = "\xED\xA0\xBD\xED\xB8\x80";
    EXPECT_FALSE(mutf8::is_canonical(pair));
    EXPECT_EQ(mutf8::canonical(pair), "\xF0\x9F\x98\x80");
    EXPECT_TRUE(mutf8::is_canonical("\xF0\x9F\x98\x80"));
    EXPECT_TRUE(mutf8::is_canonical("\xED\xA0\x80x")); // a lone surrogate stays
    EXPECT_EQ(mutf8::canonical("\xED\xA0\x80x"), "\xED\xA0\x80x");
}

TEST(Mutf8, Utf16OrderDiffersFromByteOrder)
{
    // U+FFFD (EF BF BD) is above the surrogates that encode U+1F600 in UTF-16,
    // although its UTF-8 bytes sort first.
    const std::string bmp = "\xEF\xBF\xBD";
    const std::string supplementary = "\xF0\x9F\x98\x80";
    EXPECT_LT(bmp, supplementary);
    EXPECT_GT(mutf8::compare_utf16(bmp, supplementary), 0);
    EXPECT_LT(mutf8::compare_utf16("a", "b"), 0);
    EXPECT_LT(mutf8::compare_utf16("a", "ab"), 0);
    EXPECT_EQ(mutf8::compare_utf16("same", "same"), 0);
    // The canonical and the split form of one character are the same string.
    EXPECT_EQ(mutf8::compare_utf16(supplementary, "\xED\xA0\xBD\xED\xB8\x80"), 0);
}

TEST(Mutf8, RandomCodeUnitsRoundTrip)
{
    // Any sequence of UTF-16 code units, encoded as MUTF-8, decodes to WTF-8
    // that encodes back to the same bytes.
    std::mt19937 rng(20261020);
    for (int round = 0; round < 2000; ++round) {
        std::vector<uint8_t> bytes;
        uint32_t units = rng() % 12;
        for (uint32_t i = 0; i < units; ++i) {
            uint32_t u = rng() % 4 == 0 ? 0xD800 + rng() % 0x800 : rng() % 0x10000;
            if (u != 0 && u < 0x80) {
                bytes.push_back(static_cast<uint8_t>(u));
            }
            else if (u < 0x800) {
                bytes.push_back(static_cast<uint8_t>(0xC0 | (u >> 6)));
                bytes.push_back(static_cast<uint8_t>(0x80 | (u & 0x3F)));
            }
            else {
                bytes.push_back(static_cast<uint8_t>(0xE0 | (u >> 12)));
                bytes.push_back(static_cast<uint8_t>(0x80 | ((u >> 6) & 0x3F)));
                bytes.push_back(static_cast<uint8_t>(0x80 | (u & 0x3F)));
            }
        }
        auto buf = item(units, bytes);
        auto s = mutf8::decode_string_data(buf, 0);
        ASSERT_TRUE(s.has_value());
        EXPECT_TRUE(mutf8::is_valid_wtf8(*s));
        EXPECT_TRUE(mutf8::is_canonical(*s));
        EXPECT_EQ(mutf8::utf16_length(*s), units);
        EXPECT_EQ(encode(*s), bytes);
    }
}
