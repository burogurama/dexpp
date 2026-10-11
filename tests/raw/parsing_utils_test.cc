#include "raw/parsing_utils.hpp"
#include <cstdint>
#include <gtest/gtest.h>
#include <span>
#include <vector>

using namespace dex::raw::parse_utils;

class ReadUint32Test : public ::testing::Test
{
  protected:
    void SetUp() override { offset = 0; }

    std::uint32_t offset;
};

TEST_F(ReadUint32Test, ReadsSimpleValue)
{
    std::vector<uint8_t> buf = {0x01, 0x00, 0x00, 0x00};
    auto result = read_uint32(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x01u);
    EXPECT_EQ(offset, 4u);
}

TEST_F(ReadUint32Test, ReadsMultiByteValue)
{
    std::vector<uint8_t> buf = {0x34, 0x12, 0x00, 0x00};
    auto result = read_uint32(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x1234u);
    EXPECT_EQ(offset, 4u);
}

TEST_F(ReadUint32Test, ReadsFull32BitValue)
{
    std::vector<uint8_t> buf = {0x78, 0x56, 0x34, 0x12};
    auto result = read_uint32(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x12345678u);
    EXPECT_EQ(offset, 4u);
}

TEST_F(ReadUint32Test, ReadsMaximumValue)
{
    std::vector<uint8_t> buf = {0xFF, 0xFF, 0xFF, 0xFF};
    auto result = read_uint32(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0xFFFFFFFFu);
    EXPECT_EQ(offset, 4u);
}

TEST_F(ReadUint32Test, ReadsZeroValue)
{
    std::vector<uint8_t> buf = {0x00, 0x00, 0x00, 0x00};
    auto result = read_uint32(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x00u);
    EXPECT_EQ(offset, 4u);
}

TEST_F(ReadUint32Test, ReadsSequentialValues)
{
    std::vector<uint8_t> buf = {0x01, 0x00, 0x00, 0x00, 0x02, 0x00,
                                0x00, 0x00, 0x03, 0x00, 0x00, 0x00};

    auto r1 = read_uint32(std::span(buf), offset);
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(*r1, 0x01u);
    EXPECT_EQ(offset, 4u);

    auto r2 = read_uint32(std::span(buf), offset);
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(*r2, 0x02u);
    EXPECT_EQ(offset, 8u);

    auto r3 = read_uint32(std::span(buf), offset);
    ASSERT_TRUE(r3.has_value());
    EXPECT_EQ(*r3, 0x03u);
    EXPECT_EQ(offset, 12u);
}

TEST_F(ReadUint32Test, ReadsFromMiddleOfBuffer)
{
    std::vector<uint8_t> buf = {0xAA, 0xAA, 0xAA, 0xAA, 0x78, 0x56,
                                0x34, 0x12, 0xBB, 0xBB, 0xBB, 0xBB};
    offset = 4;

    auto result = read_uint32(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x12345678u);
    EXPECT_EQ(offset, 8u);
}

TEST_F(ReadUint32Test, ReadsBigEndianLikeValueButLittleEndian)
{
    std::vector<uint8_t> buf = {0x12, 0x34, 0x56, 0x78};
    auto result = read_uint32(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x78563412u);
    EXPECT_EQ(offset, 4u);
}

TEST_F(ReadUint32Test, ReadsAllBytePatterns)
{
    std::vector<uint8_t> buf = {0xFF, 0x00, 0xFF, 0x00};
    auto result = read_uint32(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x00FF00FFu);
    EXPECT_EQ(offset, 4u);
}

TEST_F(ReadUint32Test, ReadsWithStartingOffset)
{
    std::vector<uint8_t> buf = {0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12};
    offset = 5;

    auto result = read_uint32(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x12345678u);
    EXPECT_EQ(offset, 9u);
}

TEST_F(ReadUint32Test, ReadsFourByteMagicValue)
{
    std::vector<uint8_t> buf = {0x64, 0x65, 0x78, 0x0a};
    auto result = read_uint32(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x0a786564u);
    EXPECT_EQ(offset, 4u);
}

TEST_F(ReadUint32Test, ReturnsErrorOnOutOfBounds)
{
    std::vector<uint8_t> buf = {0x01, 0x02, 0x03}; // only 3 bytes
    auto result = read_uint32(std::span(buf), offset);
    EXPECT_FALSE(result.has_value());
}

class ReadUint16Test : public ::testing::Test
{
  protected:
    void SetUp() override { offset = 0; }

    std::uint32_t offset;
};

TEST_F(ReadUint16Test, ReadsSimpleValue)
{
    std::vector<uint8_t> buf = {0x01, 0x00};
    auto result = read_uint16(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x0001u);
    EXPECT_EQ(offset, 2u);
}

TEST_F(ReadUint16Test, ReadsMultiByteValue)
{
    std::vector<uint8_t> buf = {0x34, 0x12};
    auto result = read_uint16(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x1234u);
    EXPECT_EQ(offset, 2u);
}

TEST_F(ReadUint16Test, ReadsMaximumValue)
{
    std::vector<uint8_t> buf = {0xFF, 0xFF};
    auto result = read_uint16(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0xFFFFu);
    EXPECT_EQ(offset, 2u);
}

TEST_F(ReadUint16Test, ReadsZeroValue)
{
    std::vector<uint8_t> buf = {0x00, 0x00};
    auto result = read_uint16(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x0000u);
    EXPECT_EQ(offset, 2u);
}

TEST_F(ReadUint16Test, ReadsSequentialValues)
{
    std::vector<uint8_t> buf = {0x01, 0x00, 0x02, 0x00, 0x03, 0x00};

    auto r1 = read_uint16(std::span(buf), offset);
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(*r1, 0x0001u);
    EXPECT_EQ(offset, 2u);

    auto r2 = read_uint16(std::span(buf), offset);
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(*r2, 0x0002u);
    EXPECT_EQ(offset, 4u);

    auto r3 = read_uint16(std::span(buf), offset);
    ASSERT_TRUE(r3.has_value());
    EXPECT_EQ(*r3, 0x0003u);
    EXPECT_EQ(offset, 6u);
}

TEST_F(ReadUint16Test, ReadsFromMiddleOfBuffer)
{
    std::vector<uint8_t> buf = {0xAA, 0xAA, 0x34, 0x12, 0xBB, 0xBB};
    offset = 2;

    auto result = read_uint16(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x1234u);
    EXPECT_EQ(offset, 4u);
}

TEST_F(ReadUint16Test, ReadsBigEndianLikeValueButLittleEndian)
{
    std::vector<uint8_t> buf = {0x12, 0x34};
    auto result = read_uint16(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x3412u);
    EXPECT_EQ(offset, 2u);
}

TEST_F(ReadUint16Test, ReadsAllBytePatterns)
{
    std::vector<uint8_t> buf = {0xFF, 0x00};
    auto result = read_uint16(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x00FFu);
    EXPECT_EQ(offset, 2u);
}

TEST_F(ReadUint16Test, ReadsWithStartingOffset)
{
    std::vector<uint8_t> buf = {0x00, 0x00, 0x00, 0x34, 0x12};
    offset = 3;

    auto result = read_uint16(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x1234u);
    EXPECT_EQ(offset, 5u);
}

TEST_F(ReadUint16Test, ReadsHeaderSizeField)
{
    std::vector<uint8_t> buf = {0x70, 0x00};
    auto result = read_uint16(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x0070u);
    EXPECT_EQ(offset, 2u);
}

TEST_F(ReadUint16Test, ReturnsErrorOnOutOfBounds)
{
    std::vector<uint8_t> buf = {0x01}; // only 1 byte
    auto result = read_uint16(std::span(buf), offset);
    EXPECT_FALSE(result.has_value());
}

class ReadUleb128Test : public ::testing::Test
{
  protected:
    void SetUp() override { offset = 0; }

    std::uint32_t offset;
};

TEST_F(ReadUleb128Test, ReadsZero)
{
    std::vector<uint8_t> buf = {0x00};
    auto result = read_uleb128(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0u);
    EXPECT_EQ(offset, 1u);
}

TEST_F(ReadUleb128Test, ReadsSingleByteValues)
{
    std::vector<uint8_t> buf = {0x01, 0x7F};

    auto r1 = read_uleb128(std::span(buf), offset);
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(*r1, 0x01u);
    EXPECT_EQ(offset, 1u);

    auto r2 = read_uleb128(std::span(buf), offset);
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(*r2, 0x7Fu);
    EXPECT_EQ(offset, 2u);
}

TEST_F(ReadUleb128Test, ReadsTwoByteValue128)
{
    std::vector<uint8_t> buf = {0x80, 0x01};
    auto result = read_uleb128(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 128u);
    EXPECT_EQ(offset, 2u);
}

TEST_F(ReadUleb128Test, ReadsTwoByteValue129)
{
    std::vector<uint8_t> buf = {0x81, 0x01};
    auto result = read_uleb128(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 129u);
    EXPECT_EQ(offset, 2u);
}

TEST_F(ReadUleb128Test, ReadsTwoByteMaxValue)
{
    std::vector<uint8_t> buf = {0xFF, 0x7F};
    auto result = read_uleb128(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 16383u);
    EXPECT_EQ(offset, 2u);
}

TEST_F(ReadUleb128Test, ReadsMultiByteValue)
{
    std::vector<uint8_t> buf = {0x80, 0x80, 0x01};
    auto result = read_uleb128(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 16384u);
    EXPECT_EQ(offset, 3u);
}

TEST_F(ReadUleb128Test, ReadsThreeByteMaxValue)
{
    std::vector<uint8_t> buf = {0xFF, 0xFF, 0x7F};
    auto result = read_uleb128(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 2097151u);
    EXPECT_EQ(offset, 3u);
}

TEST_F(ReadUleb128Test, ReadsFourByteValue)
{
    std::vector<uint8_t> buf = {0x80, 0x80, 0x80, 0x01};
    auto result = read_uleb128(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 2097152u);
    EXPECT_EQ(offset, 4u);
}

TEST_F(ReadUleb128Test, ReadsFiveByteMaxPossibleValue)
{
    std::vector<uint8_t> buf = {0xFF, 0xFF, 0xFF, 0xFF, 0x0F};
    auto result = read_uleb128(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 4294967295u);
    EXPECT_EQ(offset, 5u);
}

TEST_F(ReadUleb128Test, ReadsSequentialValues)
{
    std::vector<uint8_t> buf = {0x01, 0x7F, 0x80, 0x01, 0xFF, 0x7F};

    auto r1 = read_uleb128(std::span(buf), offset);
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(*r1, 0x01u);
    EXPECT_EQ(offset, 1u);

    auto r2 = read_uleb128(std::span(buf), offset);
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(*r2, 0x7Fu);
    EXPECT_EQ(offset, 2u);

    auto r3 = read_uleb128(std::span(buf), offset);
    ASSERT_TRUE(r3.has_value());
    EXPECT_EQ(*r3, 128u);
    EXPECT_EQ(offset, 4u);

    auto r4 = read_uleb128(std::span(buf), offset);
    ASSERT_TRUE(r4.has_value());
    EXPECT_EQ(*r4, 16383u);
    EXPECT_EQ(offset, 6u);
}

TEST_F(ReadUleb128Test, ReadsFromMiddleOfBuffer)
{
    std::vector<uint8_t> buf = {0xFF, 0xFF, 0xFF, 0x80, 0x80, 0x01, 0xFF, 0xFF, 0xFF};
    offset = 3;

    auto result = read_uleb128(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 16384u);
    EXPECT_EQ(offset, 6u);
}

TEST_F(ReadUleb128Test, ReadsWithStartingOffset)
{
    std::vector<uint8_t> buf = {0x00, 0x00, 0x00, 0xFF, 0x7F};
    offset = 3;

    auto result = read_uleb128(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 16383u);
    EXPECT_EQ(offset, 5u);
}

TEST_F(ReadUleb128Test, ReadsSmallUleb128Value)
{
    std::vector<uint8_t> buf = {0x05};
    auto result = read_uleb128(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 5u);
    EXPECT_EQ(offset, 1u);
}

TEST_F(ReadUleb128Test, ReadsLargeMultiByteUleb128)
{
    std::vector<uint8_t> buf = {0xE5, 0x8E, 0x26};
    auto result = read_uleb128(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 624485u);
    EXPECT_EQ(offset, 3u);
}

TEST_F(ReadUleb128Test, ReturnsErrorOnEmptyBuffer)
{
    std::vector<uint8_t> buf = {};
    auto result = read_uleb128(std::span(buf), offset);
    EXPECT_FALSE(result.has_value());
}

class DecodeMutf8Test : public ::testing::Test
{
  protected:
    void SetUp() override { offset = 0; }

    std::uint32_t offset;
};

TEST_F(DecodeMutf8Test, DecodesEmptyString)
{
    std::vector<uint8_t> buf = {0x00};
    auto result = decode_mutf8(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "");
    EXPECT_EQ(offset, 1u);
}

TEST_F(DecodeMutf8Test, DecodesSimpleAscii)
{
    std::vector<uint8_t> buf = {'H', 'e', 'l', 'l', 'o', 0x00};
    auto result = decode_mutf8(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "Hello");
    EXPECT_EQ(offset, 6u);
}

// MUTF-8 stores supplementary characters as a surrogate pair of two 3-byte
// (CESU-8) sequences; the decoder must combine them into valid 4-byte UTF-8.
TEST_F(DecodeMutf8Test, CombinesSurrogatePairToUtf8)
{
    // U+1F600 (😀) is stored as the surrogate pair D83D DE00, each a 3-byte
    // MUTF-8 sequence, then the null terminator.  Expected UTF-8: F0 9F 98 80.
    std::vector<uint8_t> buf = {0xED, 0xA0, 0xBD, 0xED, 0xB8, 0x80, 0x00};
    auto result = decode_mutf8(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, std::string("\xF0\x9F\x98\x80"));
    EXPECT_EQ(offset, 7u);
}

// A valid 3-byte BMP character round-trips unchanged.
TEST_F(DecodeMutf8Test, DecodesThreeByteBmp)
{
    // U+20AC (euro) = E2 82 AC.
    std::vector<uint8_t> buf = {0xE2, 0x82, 0xAC, 0x00};
    auto result = decode_mutf8(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, std::string("\xE2\x82\xAC"));
}

// A lone (unpaired) high surrogate becomes U+FFFD, keeping output valid UTF-8.
TEST_F(DecodeMutf8Test, LoneSurrogateBecomesReplacement)
{
    std::vector<uint8_t> buf = {0xED, 0xA7, 0xBF, 'x', 0x00}; // high surrogate, then 'x'
    auto result = decode_mutf8(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, std::string("\xEF\xBF\xBD"
                                   "x"));
}

TEST_F(DecodeMutf8Test, DecodesStringWithSpaces)
{
    std::vector<uint8_t> buf = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', 0x00};
    auto result = decode_mutf8(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "Hello World");
    EXPECT_EQ(offset, 12u);
}

TEST_F(DecodeMutf8Test, DecodesStringWithNullEncoding)
{
    std::vector<uint8_t> buf = {'H', 'i', 0xC0, 0x80, '!', 0x00};
    auto result = decode_mutf8(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    std::string expected({'H', 'i', '\0', '!'});
    EXPECT_EQ(*result, expected);
    EXPECT_EQ(offset, 6u);
}

TEST_F(DecodeMutf8Test, DecodesStringWithMultipleNulls)
{
    std::vector<uint8_t> buf = {0xC0, 0x80, 'A', 0xC0, 0x80, 'B', 0xC0, 0x80, 0x00};
    auto result = decode_mutf8(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    std::string expected({'\0', 'A', '\0', 'B', '\0'});
    EXPECT_EQ(*result, expected);
    EXPECT_EQ(offset, 9u);
}

TEST_F(DecodeMutf8Test, DecodesTwoByteUtf8Sequence)
{
    std::vector<uint8_t> buf = {0xC2, 0xA9, 0x00};
    auto result = decode_mutf8(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    std::string expected({static_cast<char>(0xC2), static_cast<char>(0xA9)});
    EXPECT_EQ(*result, expected);
    EXPECT_EQ(offset, 3u);
}

TEST_F(DecodeMutf8Test, DecodesThreeByteUtf8Sequence)
{
    std::vector<uint8_t> buf = {0xE2, 0x82, 0xAC, 0x00};
    auto result = decode_mutf8(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    std::string expected(
        {static_cast<char>(0xE2), static_cast<char>(0x82), static_cast<char>(0xAC)});
    EXPECT_EQ(*result, expected);
    EXPECT_EQ(offset, 4u);
}

TEST_F(DecodeMutf8Test, DecodesFourByteUtf8Sequence)
{
    std::vector<uint8_t> buf = {0xF0, 0x9F, 0x98, 0x80, 0x00};
    auto result = decode_mutf8(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    std::string expected({static_cast<char>(0xF0), static_cast<char>(0x9F), static_cast<char>(0x98),
                          static_cast<char>(0x80)});
    EXPECT_EQ(*result, expected);
    EXPECT_EQ(offset, 5u);
}

TEST_F(DecodeMutf8Test, DecodesMixedUtf8AndAscii)
{
    std::vector<uint8_t> buf = {'A', 0xC2, 0xA9, 'B', 0xE2, 0x82, 0xAC, 'C', 0x00};
    auto result = decode_mutf8(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    std::string expected({'A', static_cast<char>(0xC2), static_cast<char>(0xA9), 'B',
                          static_cast<char>(0xE2), static_cast<char>(0x82), static_cast<char>(0xAC),
                          'C'});
    EXPECT_EQ(*result, expected);
    EXPECT_EQ(offset, 9u);
}

TEST_F(DecodeMutf8Test, DecodesSequentialStrings)
{
    std::vector<uint8_t> buf = {'F', 'i', 'r', 's', 't', 0x00, 'S', 'e', 'c', 'o', 'n', 'd', 0x00};

    auto r1 = decode_mutf8(std::span(buf), offset);
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(*r1, "First");
    EXPECT_EQ(offset, 6u);

    auto r2 = decode_mutf8(std::span(buf), offset);
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(*r2, "Second");
    EXPECT_EQ(offset, 13u);
}

TEST_F(DecodeMutf8Test, DecodesFromMiddleOfBuffer)
{
    std::vector<uint8_t> buf = {0xFF, 0xFF, 0xFF, 'M',  'i',  'd', 'd',
                                'l',  'e',  0x00, 0xFF, 0xFF, 0xFF};
    offset = 3;

    auto result = decode_mutf8(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "Middle");
    EXPECT_EQ(offset, 10u);
}

TEST_F(DecodeMutf8Test, DecodesWithStartingOffset)
{
    std::vector<uint8_t> buf = {0x00, 0x00, 0x00, 'O', 'f', 'f', 's', 'e', 't', 0x00};
    offset = 3;

    auto result = decode_mutf8(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "Offset");
    EXPECT_EQ(offset, 10u);
}

TEST_F(DecodeMutf8Test, DecodesTypeDescriptorFormat)
{
    std::vector<uint8_t> buf = {'L', 'j', 'a', 'v', 'a', '/', 'l', 'a', 'n', 'g',
                                '/', 'O', 'b', 'j', 'e', 'c', 't', ';', 0x00};
    auto result = decode_mutf8(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "Ljava/lang/Object;");
    EXPECT_EQ(offset, 19u);
}

TEST_F(DecodeMutf8Test, DecodesStringWithNumbersAndSymbols)
{
    std::vector<uint8_t> buf = {'T', 'e', 's', 't', '1', '2', '3', '!', '@',
                                '#', '$', '%', '^', '&', '*', '(', ')', 0x00};
    auto result = decode_mutf8(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "Test123!@#$%^&*()");
    EXPECT_EQ(offset, 18u);
}

TEST_F(DecodeMutf8Test, DecodesOnlyNullEncoding)
{
    std::vector<uint8_t> buf = {0xC0, 0x80, 0x00};
    auto result = decode_mutf8(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, std::string("\0", 1));
    EXPECT_EQ(offset, 3u);
}

TEST_F(DecodeMutf8Test, DecodesStringStartingWithNullEncoding)
{
    std::vector<uint8_t> buf = {0xC0, 0x80, 'S', 't', 'a', 'r', 't', 0x00};
    auto result = decode_mutf8(std::span(buf), offset);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, std::string("\0Start", 6));
    EXPECT_EQ(offset, 8u);
}

TEST_F(DecodeMutf8Test, ReturnsErrorOnMissingNullTerminator)
{
    std::vector<uint8_t> buf = {'A', 'B', 'C'}; // no null terminator
    auto result = decode_mutf8(std::span(buf), offset);
    EXPECT_FALSE(result.has_value());
}
