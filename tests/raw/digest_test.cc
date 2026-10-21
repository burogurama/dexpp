// SHA-1 / SHA-256 (raw/digest.hpp) and the DEX header checksum and signature.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "raw/digest.hpp"
#include "raw/parser.hpp"
#include "test_utils.hpp"

using namespace dex::raw;

namespace {

template <std::size_t N> std::string hex(const std::array<uint8_t, N> &digest)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (uint8_t b : digest) {
        out += digits[b >> 4];
        out += digits[b & 0xF];
    }
    return out;
}

std::vector<uint8_t> bytes(std::string_view s) { return {s.begin(), s.end()}; }

} // namespace

TEST(Digest, KnownVectors)
{
    EXPECT_EQ(hex(sha1(bytes(""))), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    EXPECT_EQ(hex(sha1(bytes("abc"))), "a9993e364706816aba3e25717850c26c9cd0d89d");
    EXPECT_EQ(hex(sha256(bytes(""))),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(hex(sha256(bytes("abc"))),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    // Two-block padding: 56 bytes leave no room for the length in one block.
    std::string fifty_six = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    EXPECT_EQ(hex(sha1(bytes(fifty_six))), "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    EXPECT_EQ(hex(sha256(bytes(fifty_six))),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // Full blocks read straight from the input, plus a tail.
    std::vector<uint8_t> million(1000000, 'a');
    EXPECT_EQ(hex(sha1(million)), "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}

TEST(Digest, HeaderChecksumAndSignatureOfFixtures)
{
    // (polymorphic.dex is left out: the tool that built it left a stale
    // signature, which ART does not check.)
    for (const char *path : {"tests/data/classes.dex", "tests/data/inherit.dex",
                             "tests/data/fibonacci.dex", "tests/data/androguard/classes.dex"}) {
        auto buf = load_buf(path);
        auto header = parser::parse_header(buf);
        ASSERT_TRUE(header.has_value()) << path;
        auto checksum = parser::compute_checksum(buf);
        ASSERT_TRUE(checksum.has_value());
        EXPECT_EQ(*checksum, header->checksum) << path;
        auto signature = parser::compute_signature(buf);
        ASSERT_TRUE(signature.has_value());
        EXPECT_TRUE(std::equal(signature->begin(), signature->end(), header->signature)) << path;
    }
}
