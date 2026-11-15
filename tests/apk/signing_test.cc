// Tests for APK signature parsing, against tests/data/signed.apk — sample.apk
// signed with apksigner (v1 + v2 + v3) using a throwaway RSA-2048 key:
//   CN=Dexpp Test, O=DexppOrg, C=MA, serial ba51e610667560a3.
// The fingerprint below is ground truth from:
//   unzip -p signed.apk 'META-INF/*.RSA' | openssl pkcs7 -inform DER -print_certs
//     | openssl x509 -fingerprint -sha256 -noout

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <vector>

#include "dex.hpp"
#include "raw/zip.hpp"

using namespace dex;
using namespace dex::apk;

namespace {

constexpr const char *kExpectedSha256 =
    "6c9647205d1ae141f9104587153cb1c71c5da9251335da1a1977de500c6ef599";

const SigningInfo &signed_info()
{
    static auto apk = []() {
        auto a = Apk::open("tests/data/signed.apk");
        if (!a.has_value())
            throw std::runtime_error(a.error().message);
        return std::move(*a);
    }();
    return apk.signing();
}

} // namespace

TEST(SigningTest, AllThreeSchemesDetected)
{
    const SigningInfo &info = signed_info();
    EXPECT_TRUE(info.v1);
    EXPECT_TRUE(info.v2);
    EXPECT_TRUE(info.v3);
    EXPECT_TRUE(info.is_signed());
}

TEST(SigningTest, CertificateDeduplicatedAcrossSchemes)
{
    // The same certificate appears in the v1 PKCS#7 blob, the v2 block, and
    // the v3 block — it must surface exactly once.
    EXPECT_EQ(signed_info().certificates.size(), 1u);
}

TEST(SigningTest, CertificateFields)
{
    ASSERT_EQ(signed_info().certificates.size(), 1u);
    const Certificate &cert = signed_info().certificates[0];

    EXPECT_EQ(cert.subject, "C=MA, O=DexppOrg, CN=Dexpp Test");
    EXPECT_EQ(cert.issuer, cert.subject);
    EXPECT_TRUE(cert.self_signed());
    EXPECT_EQ(cert.serial_hex, "ba51e610667560a3");
    EXPECT_EQ(cert.not_before, "2026-06-11T13:54:30Z");
    EXPECT_EQ(cert.not_after, "2053-10-27T13:54:30Z");
    EXPECT_FALSE(cert.der.empty());
}

TEST(SigningTest, FingerprintsMatchOpenssl)
{
    ASSERT_EQ(signed_info().certificates.size(), 1u);
    const Certificate &cert = signed_info().certificates[0];

    EXPECT_EQ(cert.sha256_hex, kExpectedSha256);
    EXPECT_EQ(cert.sha1_hex.size(), 40u);
    EXPECT_EQ(cert.sha256_hex.size(), 64u);
}

TEST(SigningTest, UnsignedApkYieldsNothing)
{
    auto apk = Apk::open("tests/data/sample.apk");
    ASSERT_TRUE(apk.has_value());

    const SigningInfo &info = apk->signing();
    EXPECT_FALSE(info.v1);
    EXPECT_FALSE(info.v2);
    EXPECT_FALSE(info.v3);
    EXPECT_FALSE(info.is_signed());
    EXPECT_TRUE(info.certificates.empty());
}

TEST(SigningTest, SignedApkStillAnalyzable)
{
    // Signing must not interfere with the rest of the APK surface.
    auto apk = Apk::open("tests/data/signed.apk");
    ASSERT_TRUE(apk.has_value());

    ASSERT_NE(apk->manifest(), nullptr);
    EXPECT_EQ(apk->manifest()->package(), "com.example.dexpp");

    auto ctx = apk->analysis();
    ASSERT_TRUE(ctx.has_value());
    EXPECT_TRUE(ctx->find_class("LTestDex;").has_value());
}

// ===== damaged APK Signing Blocks =====
//
// The sizes in the signing block come from the file.  A huge size used to
// wrap around in a bounds check written as `a + b > c`, and the parser then
// read far outside the APK (found by the fuzz_signing harness).  These tests
// patch signed.apk in memory; under ASan an out-of-bounds read fails them.

namespace {

std::vector<uint8_t> read_signed_apk()
{
    std::ifstream f("tests/data/signed.apk", std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

void put_le64(std::vector<uint8_t> &buf, std::size_t pos, uint64_t value)
{
    for (int i = 0; i < 8; ++i)
        buf[pos + static_cast<std::size_t>(i)] = static_cast<uint8_t>(value >> (8 * i));
}

uint64_t get_le64(const std::vector<uint8_t> &buf, std::size_t pos)
{
    uint64_t value = 0;
    for (int i = 7; i >= 0; --i)
        value = (value << 8) | buf[pos + static_cast<std::size_t>(i)];
    return value;
}

/** Offsets in signed.apk: the central directory, and the signing block. */
struct BlockLayout
{
    std::size_t cd_off;
    std::size_t block_start; // the leading size field
    std::size_t first_pair;  // the first id-value pair's length field
};

BlockLayout layout_of(const std::vector<uint8_t> &apk)
{
    auto cd = dex::raw::zip::central_directory_offset(apk);
    EXPECT_TRUE(cd.has_value());
    const std::size_t cd_off = *cd;
    const uint64_t block_size = get_le64(apk, cd_off - 24);
    const std::size_t block_start = cd_off - 8 - static_cast<std::size_t>(block_size);
    return {cd_off, block_start, block_start + 8};
}

} // namespace

TEST(SigningDamagedBlock, TheUnpatchedFixtureHasTheExpectedLayout)
{
    std::vector<uint8_t> apk = read_signed_apk();
    ASSERT_FALSE(apk.empty());
    const BlockLayout at = layout_of(apk);
    EXPECT_EQ(get_le64(apk, at.block_start), get_le64(apk, at.cd_off - 24));
    EXPECT_TRUE(dex::apk::parse_signing_info(apk).v2);
}

TEST(SigningDamagedBlock, AHugePairLengthIsRejected)
{
    std::vector<uint8_t> apk = read_signed_apk();
    const BlockLayout at = layout_of(apk);
    for (uint64_t huge : {UINT64_MAX, UINT64_MAX - 7, uint64_t{1} << 63}) {
        std::vector<uint8_t> patched = apk;
        put_le64(patched, at.first_pair, huge);
        dex::apk::SigningInfo info = dex::apk::parse_signing_info(patched);
        EXPECT_FALSE(info.v2) << huge; // the bad pair stops the walk
        EXPECT_TRUE(info.v1) << huge;  // the JAR signature is still found
    }
}

TEST(SigningDamagedBlock, AHugeBlockSizeIsRejected)
{
    std::vector<uint8_t> apk = read_signed_apk();
    const BlockLayout at = layout_of(apk);
    for (uint64_t huge : {UINT64_MAX, UINT64_MAX - 7, uint64_t{1} << 63}) {
        std::vector<uint8_t> patched = apk;
        put_le64(patched, at.cd_off - 24, huge);
        dex::apk::SigningInfo info = dex::apk::parse_signing_info(patched);
        EXPECT_FALSE(info.v2) << huge;
        EXPECT_FALSE(info.v3) << huge;
    }
}
