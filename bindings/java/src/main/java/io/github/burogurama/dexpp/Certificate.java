package io.github.burogurama.dexpp;

import java.util.Arrays;
import java.util.Objects;

/**
 * An X.509 certificate that signed an APK.
 *
 * <p>{@link #der()} returns a copy of the bytes, and {@code equals} compares their content, so the
 * record stays a plain immutable value.
 *
 * @param der the certificate in its binary (DER) form
 * @param subject who the certificate is for, such as {@code "C=US, O=Example, CN=App"}
 * @param issuer who issued it, in the same form; equal to {@code subject} when self-signed
 * @param serialHex the serial number, in lowercase hex
 * @param notBefore the start of the validity period, as {@code "YYYY-MM-DDTHH:MM:SSZ"}
 * @param notAfter the end of the validity period, in the same form
 * @param sha1Hex the SHA-1 fingerprint of the DER bytes, in lowercase hex
 * @param sha256Hex the SHA-256 fingerprint of the DER bytes, in lowercase hex
 * @param selfSigned true when the subject and the issuer are the same
 */
public record Certificate(
        byte[] der,
        String subject,
        String issuer,
        String serialHex,
        String notBefore,
        String notAfter,
        String sha1Hex,
        String sha256Hex,
        boolean selfSigned) {
    /** Checks the parts, and copies the DER bytes. */
    public Certificate {
        der = der.clone();
        Objects.requireNonNull(subject, "subject");
        Objects.requireNonNull(issuer, "issuer");
        Objects.requireNonNull(serialHex, "serialHex");
        Objects.requireNonNull(notBefore, "notBefore");
        Objects.requireNonNull(notAfter, "notAfter");
        Objects.requireNonNull(sha1Hex, "sha1Hex");
        Objects.requireNonNull(sha256Hex, "sha256Hex");
    }

    /**
     * The certificate in its binary (DER) form.
     *
     * @return a copy of the bytes
     */
    @Override
    public byte[] der() {
        return der.clone();
    }

    @Override
    public boolean equals(Object other) {
        return other instanceof Certificate c
                && Arrays.equals(der, c.der)
                && subject.equals(c.subject)
                && issuer.equals(c.issuer)
                && serialHex.equals(c.serialHex)
                && notBefore.equals(c.notBefore)
                && notAfter.equals(c.notAfter)
                && sha1Hex.equals(c.sha1Hex)
                && sha256Hex.equals(c.sha256Hex)
                && selfSigned == c.selfSigned;
    }

    @Override
    public int hashCode() {
        return Objects.hash(
                Arrays.hashCode(der),
                subject,
                issuer,
                serialHex,
                notBefore,
                notAfter,
                sha1Hex,
                sha256Hex,
                selfSigned);
    }

    @Override
    public String toString() {
        return "Certificate[subject="
                + subject
                + ", issuer="
                + issuer
                + ", serialHex="
                + serialHex
                + ", notBefore="
                + notBefore
                + ", notAfter="
                + notAfter
                + ", sha256Hex="
                + sha256Hex
                + ", der="
                + der.length
                + " bytes]";
    }
}
