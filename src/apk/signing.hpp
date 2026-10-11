#pragma once

/** APK signature inspection: which signing schemes are present (v1 JAR
 *  signing, v2/v3 APK Signing Block) and the signing certificates.
 *
 *  Scope is identification, not verification — the parser extracts the
 *  certificates and reports the schemes, it does NOT validate digests or
 *  signatures.  Use apksigner for verification. */

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace dex::apk {

/** A signing certificate (X.509, decoded just enough for triage). */
struct Certificate
{
    std::vector<uint8_t> der; ///< Full DER encoding.

    std::string subject;    ///< e.g. "CN=Foo, O=Bar, C=US" (recognized RDNs only).
    std::string issuer;     ///< Same form; equal to subject for self-signed certs.
    std::string serial_hex; ///< Serial number, lowercase hex, no sign byte.
    std::string not_before; ///< "YYYY-MM-DDTHH:MM:SSZ" (best effort).
    std::string not_after;

    std::string sha1_hex;   ///< Fingerprint of `der`, lowercase hex.
    std::string sha256_hex; ///< The standard certificate identity for pinning.

    bool self_signed() const { return !subject.empty() && subject == issuer; }
};

/** Signing schemes present in an APK and the certificates they carry. */
struct SigningInfo
{
    bool v1 = false; ///< JAR signing: META-INF/*.RSA / *.DSA / *.EC present.
    bool v2 = false; ///< APK Signature Scheme v2 block present.
    bool v3 = false; ///< v3 or v3.1 block present.

    /** Signing certificates across all schemes, de-duplicated by SHA-256.
     *  Empty for unsigned APKs. */
    std::vector<Certificate> certificates;

    bool is_signed() const { return v1 || v2 || v3; }
};

/** Extract signing information from a whole-APK (ZIP) image.  Never fails:
 *  an unsigned or unparseable input yields a default SigningInfo. */
SigningInfo parse_signing_info(std::span<const uint8_t> apk_bytes);

} // namespace dex::apk
