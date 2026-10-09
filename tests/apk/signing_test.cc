// Tests for APK signature parsing, against tests/data/signed.apk — sample.apk
// signed with apksigner (v1 + v2 + v3) using a throwaway RSA-2048 key:
//   CN=Dexpp Test, O=DexppOrg, C=MA, serial ba51e610667560a3.
// The fingerprint below is ground truth from:
//   unzip -p signed.apk 'META-INF/*.RSA' | openssl pkcs7 -inform DER -print_certs \
//     | openssl x509 -fingerprint -sha256 -noout

#include <gtest/gtest.h>

#include "dex.hpp"

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
