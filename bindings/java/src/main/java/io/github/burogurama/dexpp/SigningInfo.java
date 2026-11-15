package io.github.burogurama.dexpp;

import java.util.List;

/**
 * Which signature schemes an APK carries, and the certificates in them. This is identification
 * only: the signatures and digests are not verified.
 *
 * @param v1 a JAR signature: a {@code META-INF/*.RSA}, {@code *.DSA} or {@code *.EC} file
 * @param v2 an APK Signature Scheme v2 block
 * @param v3 an APK Signature Scheme v3 or v3.1 block
 * @param certificates the signing certificates of all schemes. A certificate that appears in
 *     several schemes is listed once. Empty for an unsigned APK.
 */
public record SigningInfo(boolean v1, boolean v2, boolean v3, List<Certificate> certificates) {
    /** Copies the certificates into an immutable list. */
    public SigningInfo {
        certificates = List.copyOf(certificates);
    }

    /**
     * Tells whether at least one signature scheme is present.
     *
     * @return true for a signed APK
     */
    public boolean isSigned() {
        return v1 || v2 || v3;
    }
}
