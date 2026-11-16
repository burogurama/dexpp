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
 *     counts as normal. For a reference, it is the default configuration's value.
 * @param protectionLevelVaries true when the level is a reference whose value is not the same in
 *     every configuration. The permission then guards a component only when every configuration
 *     gives signature or signatureOrSystem.
 */
public record DeclaredPermission(
        String name, OptionalInt protectionLevel, boolean protectionLevelVaries) {
    /** Checks the parts. */
    public DeclaredPermission {
        Objects.requireNonNull(name, "name");
        Objects.requireNonNull(protectionLevel, "protectionLevel");
    }
}
