#include "apk/signing.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstring>
#include <optional>
#include <unordered_set>
#include <vector>

#include "raw/zip.hpp"

namespace dex::apk {

namespace {

// ===== hashing (self-contained; fingerprints only, not verification) =====

std::string to_hex(std::span<const uint8_t> bytes)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (uint8_t b : bytes) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0xF]);
    }
    return out;
}

std::string sha1_hex(std::span<const uint8_t> data)
{
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};

    uint64_t bitlen = static_cast<uint64_t>(data.size()) * 8;
    std::vector<uint8_t> msg(data.begin(), data.end());
    msg.push_back(0x80);
    while (msg.size() % 64 != 56)
        msg.push_back(0);
    for (int i = 7; i >= 0; --i)
        msg.push_back(static_cast<uint8_t>(bitlen >> (8 * i)));

    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (static_cast<uint32_t>(msg[chunk + i * 4]) << 24) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 2]) << 8) | msg[chunk + i * 4 + 3];
        for (int i = 16; i < 80; ++i)
            w[i] = std::rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            }
            else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            }
            else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            }
            else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t tmp = std::rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = std::rotl(b, 30);
            b = a;
            a = tmp;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }

    uint8_t digest[20];
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 4; ++j)
            digest[i * 4 + j] = static_cast<uint8_t>(h[i] >> (24 - 8 * j));
    return to_hex(digest);
}

std::string sha256_hex(std::span<const uint8_t> data)
{
    static constexpr uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};

    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    uint64_t bitlen = static_cast<uint64_t>(data.size()) * 8;
    std::vector<uint8_t> msg(data.begin(), data.end());
    msg.push_back(0x80);
    while (msg.size() % 64 != 56)
        msg.push_back(0);
    for (int i = 7; i >= 0; --i)
        msg.push_back(static_cast<uint8_t>(bitlen >> (8 * i)));

    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (static_cast<uint32_t>(msg[chunk + i * 4]) << 24) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 2]) << 8) | msg[chunk + i * 4 + 3];
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }

    uint8_t digest[32];
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 4; ++j)
            digest[i * 4 + j] = static_cast<uint8_t>(h[i] >> (24 - 8 * j));
    return to_hex(digest);
}

// ===== minimal DER walker =====

struct Tlv
{
    uint8_t tag = 0;
    std::span<const uint8_t> content;
    std::span<const uint8_t> full; ///< Header + content (the whole encoding).
};

std::optional<Tlv> read_tlv(std::span<const uint8_t> buf, std::size_t &pos)
{
    if (pos + 2 > buf.size())
        return std::nullopt;
    Tlv out;
    std::size_t start = pos;
    out.tag = buf[pos++];

    uint64_t len = buf[pos++];
    if (len & 0x80) {
        unsigned nbytes = len & 0x7F;
        if (nbytes == 0 || nbytes > 4 || pos + nbytes > buf.size())
            return std::nullopt;
        len = 0;
        for (unsigned i = 0; i < nbytes; ++i)
            len = (len << 8) | buf[pos++];
    }
    if (pos + len > buf.size())
        return std::nullopt;

    out.content = buf.subspan(pos, len);
    out.full = buf.subspan(start, pos + len - start);
    pos += len;
    return out;
}

// ===== X.509 (just the triage fields) =====

std::string oid_short_name(std::span<const uint8_t> oid)
{
    struct Known
    {
        std::initializer_list<uint8_t> bytes;
        const char *name;
    };
    static const Known kKnown[] = {
        {{0x55, 0x04, 0x03}, "CN"},
        {{0x55, 0x04, 0x06}, "C"},
        {{0x55, 0x04, 0x07}, "L"},
        {{0x55, 0x04, 0x08}, "ST"},
        {{0x55, 0x04, 0x0A}, "O"},
        {{0x55, 0x04, 0x0B}, "OU"},
        {{0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x09, 0x01}, "E"},
    };
    for (const auto &k : kKnown) {
        if (oid.size() == k.bytes.size() && std::equal(oid.begin(), oid.end(), k.bytes.begin()))
            return k.name;
    }
    return {};
}

/** Render an X.501 Name (SEQUENCE OF SET OF AttributeTypeAndValue) as
 *  "CN=..., O=..." with recognized attribute types only, in DER order. */
std::string render_name(std::span<const uint8_t> name_content)
{
    std::string out;
    std::size_t pos = 0;
    while (auto set = read_tlv(name_content, pos)) {
        std::size_t set_pos = 0;
        while (auto atv = read_tlv(set->content, set_pos)) {
            std::size_t atv_pos = 0;
            auto oid = read_tlv(atv->content, atv_pos);
            auto val = read_tlv(atv->content, atv_pos);
            if (!oid || !val || oid->tag != 0x06)
                continue;
            std::string key = oid_short_name(oid->content);
            if (key.empty())
                continue;
            if (!out.empty())
                out += ", ";
            out += key;
            out += '=';
            out.append(reinterpret_cast<const char *>(val->content.data()), val->content.size());
        }
    }
    return out;
}

/** Normalize UTCTime/GeneralizedTime to "YYYY-MM-DDTHH:MM:SSZ" (best effort). */
std::string render_time(const Tlv &t)
{
    std::string raw(reinterpret_cast<const char *>(t.content.data()), t.content.size());
    std::string full;
    if (t.tag == 0x17 && raw.size() >= 12) { // UTCTime: YYMMDDHHMMSSZ
        int yy = (raw[0] - '0') * 10 + (raw[1] - '0');
        full = (yy < 50 ? "20" : "19") + raw;
    }
    else if (t.tag == 0x18 && raw.size() >= 14) { // GeneralizedTime: YYYYMMDDHHMMSSZ
        full = raw;
    }
    else {
        return raw;
    }
    if (full.size() < 14)
        return raw;
    return full.substr(0, 4) + "-" + full.substr(4, 2) + "-" + full.substr(6, 2) + "T" +
           full.substr(8, 2) + ":" + full.substr(10, 2) + ":" + full.substr(12, 2) + "Z";
}

/** Decode one DER X.509 certificate into the triage fields. */
std::optional<Certificate> parse_certificate(std::span<const uint8_t> der)
{
    std::size_t pos = 0;
    auto cert_seq = read_tlv(der, pos);
    if (!cert_seq || cert_seq->tag != 0x30)
        return std::nullopt;

    std::size_t cpos = 0;
    auto tbs = read_tlv(cert_seq->content, cpos);
    if (!tbs || tbs->tag != 0x30)
        return std::nullopt;

    Certificate out;
    out.der.assign(der.begin(), der.end());
    out.sha1_hex = sha1_hex(der);
    out.sha256_hex = sha256_hex(der);

    std::size_t tpos = 0;
    auto field = read_tlv(tbs->content, tpos);
    if (field && field->tag == 0xA0) // explicit [0] version
        field = read_tlv(tbs->content, tpos);
    if (!field || field->tag != 0x02) // serialNumber INTEGER
        return out;                   // fingerprints still useful
    {
        auto serial = field->content;
        if (serial.size() > 1 && serial[0] == 0x00)
            serial = serial.subspan(1); // sign byte
        out.serial_hex = to_hex(serial);
    }

    read_tlv(tbs->content, tpos); // signature AlgorithmIdentifier (skipped)

    if (auto issuer = read_tlv(tbs->content, tpos); issuer && issuer->tag == 0x30)
        out.issuer = render_name(issuer->content);

    if (auto validity = read_tlv(tbs->content, tpos); validity && validity->tag == 0x30) {
        std::size_t vpos = 0;
        if (auto nb = read_tlv(validity->content, vpos))
            out.not_before = render_time(*nb);
        if (auto na = read_tlv(validity->content, vpos))
            out.not_after = render_time(*na);
    }

    if (auto subject = read_tlv(tbs->content, tpos); subject && subject->tag == 0x30)
        out.subject = render_name(subject->content);

    return out;
}

// ===== v1: PKCS#7 / CMS signature files =====

/** Extract every X.509 certificate from a PKCS#7 SignedData blob
 *  (META-INF/CERT.RSA and friends). */
void certs_from_pkcs7(std::span<const uint8_t> blob, std::vector<Certificate> &out)
{
    std::size_t pos = 0;
    auto content_info = read_tlv(blob, pos);
    if (!content_info || content_info->tag != 0x30)
        return;

    std::size_t cpos = 0;
    auto oid = read_tlv(content_info->content, cpos);
    auto wrapper = read_tlv(content_info->content, cpos);
    if (!oid || oid->tag != 0x06 || !wrapper || wrapper->tag != 0xA0)
        return;

    std::size_t wpos = 0;
    auto signed_data = read_tlv(wrapper->content, wpos);
    if (!signed_data || signed_data->tag != 0x30)
        return;

    // SignedData ::= version, digestAlgorithms, encapContentInfo,
    //                [0] certificates OPTIONAL, ...  — first [0] is the certs.
    std::size_t spos = 0;
    while (auto field = read_tlv(signed_data->content, spos)) {
        if (field->tag != 0xA0)
            continue;
        std::size_t certs_pos = 0;
        while (auto cert = read_tlv(field->content, certs_pos)) {
            if (cert->tag != 0x30)
                continue;
            if (auto parsed = parse_certificate(cert->full))
                out.push_back(std::move(*parsed));
        }
        break;
    }
}

bool is_v1_signature_name(std::string_view name)
{
    if (!name.starts_with("META-INF/"))
        return false;
    auto ends_with_ci = [&](std::string_view suffix) {
        if (name.size() < suffix.size())
            return false;
        auto tail = name.substr(name.size() - suffix.size());
        return std::equal(tail.begin(), tail.end(), suffix.begin(), [](char a, char b) {
            return std::toupper(static_cast<unsigned char>(a)) == b;
        });
    };
    return ends_with_ci(".RSA") || ends_with_ci(".DSA") || ends_with_ci(".EC");
}

// ===== v2/v3: APK Signing Block =====

constexpr uint32_t V2_BLOCK_ID = 0x7109871a;
constexpr uint32_t V3_BLOCK_ID = 0xf05368c0;
constexpr uint32_t V31_BLOCK_ID = 0x1b93ad61;
constexpr char SIG_BLOCK_MAGIC[16] = {'A', 'P', 'K', ' ', 'S', 'i', 'g', ' ',
                                      'B', 'l', 'o', 'c', 'k', ' ', '4', '2'};

uint32_t le32(std::span<const uint8_t> buf, std::size_t pos)
{
    return static_cast<uint32_t>(buf[pos]) | (static_cast<uint32_t>(buf[pos + 1]) << 8) |
           (static_cast<uint32_t>(buf[pos + 2]) << 16) |
           (static_cast<uint32_t>(buf[pos + 3]) << 24);
}

uint64_t le64(std::span<const uint8_t> buf, std::size_t pos)
{
    return static_cast<uint64_t>(le32(buf, pos)) |
           (static_cast<uint64_t>(le32(buf, pos + 4)) << 32);
}

/** Read one u32-length-prefixed slice, advancing @p pos; nullopt on overrun. */
std::optional<std::span<const uint8_t>> read_lp(std::span<const uint8_t> buf, std::size_t &pos)
{
    if (pos + 4 > buf.size())
        return std::nullopt;
    uint32_t len = le32(buf, pos);
    pos += 4;
    if (pos + len > buf.size())
        return std::nullopt;
    auto out = buf.subspan(pos, len);
    pos += len;
    return out;
}

/** Pull certificates out of one v2/v3 scheme block.  Both share the layout
 *  signers -> signer -> signed data -> [digests][certificates] up to the
 *  point we need. */
void certs_from_scheme_block(std::span<const uint8_t> block, std::vector<Certificate> &out)
{
    std::size_t pos = 0;
    auto signers = read_lp(block, pos);
    if (!signers)
        return;

    std::size_t signers_pos = 0;
    while (auto signer = read_lp(*signers, signers_pos)) {
        std::size_t signer_pos = 0;
        auto signed_data = read_lp(*signer, signer_pos);
        if (!signed_data)
            continue;

        std::size_t sd_pos = 0;
        if (!read_lp(*signed_data, sd_pos)) // digests (skipped)
            continue;
        auto certs = read_lp(*signed_data, sd_pos);
        if (!certs)
            continue;

        std::size_t certs_pos = 0;
        while (auto cert_der = read_lp(*certs, certs_pos)) {
            if (auto parsed = parse_certificate(*cert_der))
                out.push_back(std::move(*parsed));
        }
    }
}

} // namespace

SigningInfo parse_signing_info(std::span<const uint8_t> apk_bytes)
{
    SigningInfo info;

    // ---- v1: JAR signature files in META-INF/ ----
    if (auto names = raw::zip::entry_names(apk_bytes); names.has_value()) {
        for (const auto &name : *names) {
            if (!is_v1_signature_name(name))
                continue;
            info.v1 = true;
            if (auto blob = raw::zip::read_entry(apk_bytes, name); blob.has_value())
                certs_from_pkcs7(std::span<const uint8_t>(*blob), info.certificates);
        }
    }

    // ---- v2/v3: APK Signing Block just before the central directory ----
    auto cd_off_result = raw::zip::central_directory_offset(apk_bytes);
    if (cd_off_result.has_value()) {
        std::size_t cd_off = *cd_off_result;
        if (cd_off >= 32 && cd_off <= apk_bytes.size() &&
            std::memcmp(apk_bytes.data() + cd_off - 16, SIG_BLOCK_MAGIC, 16) == 0) {
            uint64_t block_size = le64(apk_bytes, cd_off - 24);
            // block_size excludes the leading size field; sanity-bound it.
            if (block_size >= 24 && block_size + 8 <= cd_off) {
                std::size_t block_start = cd_off - 8 - static_cast<std::size_t>(block_size);
                if (le64(apk_bytes, block_start) == block_size) {
                    std::size_t pos = block_start + 8;
                    std::size_t pairs_end = cd_off - 24;
                    while (pos + 12 <= pairs_end) {
                        uint64_t pair_len = le64(apk_bytes, pos);
                        if (pair_len < 4 || pos + 8 + pair_len > pairs_end)
                            break;
                        uint32_t id = le32(apk_bytes, pos + 8);
                        auto payload =
                            apk_bytes.subspan(pos + 12, static_cast<std::size_t>(pair_len) - 4);

                        if (id == V2_BLOCK_ID) {
                            info.v2 = true;
                            certs_from_scheme_block(payload, info.certificates);
                        }
                        else if (id == V3_BLOCK_ID || id == V31_BLOCK_ID) {
                            info.v3 = true;
                            certs_from_scheme_block(payload, info.certificates);
                        }
                        pos += 8 + static_cast<std::size_t>(pair_len);
                    }
                }
            }
        }
    }

    // ---- de-duplicate certificates across schemes by identity ----
    std::unordered_set<std::string> seen;
    std::vector<Certificate> unique;
    for (auto &cert : info.certificates) {
        if (seen.insert(cert.sha256_hex).second)
            unique.push_back(std::move(cert));
    }
    info.certificates = std::move(unique);

    return info;
}

} // namespace dex::apk
