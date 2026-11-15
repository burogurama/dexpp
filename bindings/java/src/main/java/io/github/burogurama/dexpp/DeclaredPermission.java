package io.github.burogurama.dexpp;

import java.util.Objects;
import java.util.OptionalInt;

/**
 * A {@code <permission>} that the app itself declares.
 *
 * @param name the permission name
 * @param protectionLevel the {@code android:protectionLevel} as the raw Android value, or empty
 *     when it is not known: the attribute is a resource reference that could not be resolved. A
 *     component guarded by a permission with an unknown level is not counted as guarded. The low
 *     four bits are the base level: 0 normal, 1 dangerous, 2
 *     signature, 3 signatureOrSystem. Higher bits are flags. A declaration without the attribute
 *     counts as normal.
 */
public record DeclaredPermission(String name, OptionalInt protectionLevel) {
    /** Checks the parts. */
    public DeclaredPermission {
        Objects.requireNonNull(name, "name");
        Objects.requireNonNull(protectionLevel, "protectionLevel");
    }
}
