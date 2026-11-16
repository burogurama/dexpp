package io.github.burogurama.dexpp;

import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.OptionalInt;
import java.util.OptionalLong;

/**
 * The decoded {@code AndroidManifest.xml} of an {@link Apk}. A value the manifest may leave out is
 * empty when it is not set.
 *
 * <p>An attribute can be written as a resource reference, such as {@code
 * android:debuggable="@bool/debug"}. Its value is then the one the APK's resource table gives, in
 * the default configuration, and the matching {@code ...Reference} component holds the reference
 * id. When the reference cannot be resolved, the value is empty and the {@code ...Reference}
 * component is still set: the attribute is written, but its value is unknown. A {@code
 * ...Reference} component is empty for a plain value or a missing attribute.
 *
 * <p>A boolean whose resource has another value in some configuration (a language, a screen size,
 * an Android version) has a matching {@code ...Varies} component that is true. Integers hold the
 * default configuration's value. Text, such as {@code versionName}, is empty when its string
 * changes with the configuration.
 *
 * @param packageName the {@code package} attribute of {@code <manifest>}
 * @param versionCode {@code android:versionCode}
 * @param versionCodeReference the reference id of {@code android:versionCode}
 * @param versionName {@code android:versionName}
 * @param versionNameReference the reference id of {@code android:versionName}
 * @param minSdk {@code android:minSdkVersion} of {@code <uses-sdk>}
 * @param minSdkReference the reference id of {@code android:minSdkVersion}
 * @param targetSdk {@code android:targetSdkVersion} of {@code <uses-sdk>}
 * @param targetSdkReference the reference id of {@code android:targetSdkVersion}
 * @param permissions the permissions the app asks for: the name of each {@code <uses-permission>}
 * @param declaredPermissions the permissions the app itself declares, in manifest order
 * @param components every activity, service, receiver and provider, in manifest order
 * @param launcherActivity the name of the activity the launcher starts
 * @param debuggable {@code android:debuggable} of {@code <application>}
 * @param debuggableReference the reference id of {@code android:debuggable}
 * @param debuggableVaries true when {@code android:debuggable} is a reference whose value is not
 *     the same in every configuration; {@code debuggable} holds the default configuration's value.
 *     For triage, treat a varying value as possibly true.
 * @param allowBackup {@code android:allowBackup} of {@code <application>}
 * @param allowBackupReference the reference id of {@code android:allowBackup}
 * @param allowBackupVaries true when {@code android:allowBackup} is a reference whose value is not
 *     the same in every configuration, like {@code debuggableVaries}
 */
public record Manifest(
        String packageName,
        OptionalLong versionCode,
        OptionalInt versionCodeReference,
        Optional<String> versionName,
        OptionalInt versionNameReference,
        OptionalLong minSdk,
        OptionalInt minSdkReference,
        OptionalLong targetSdk,
        OptionalInt targetSdkReference,
        List<String> permissions,
        List<DeclaredPermission> declaredPermissions,
        List<Component> components,
        Optional<String> launcherActivity,
        Optional<Boolean> debuggable,
        OptionalInt debuggableReference,
        boolean debuggableVaries,
        Optional<Boolean> allowBackup,
        OptionalInt allowBackupReference,
        boolean allowBackupVaries) {
    /** Checks the parts, and copies the lists into immutable lists. */
    public Manifest {
        Objects.requireNonNull(packageName, "packageName");
        Objects.requireNonNull(versionCode, "versionCode");
        Objects.requireNonNull(versionCodeReference, "versionCodeReference");
        Objects.requireNonNull(versionName, "versionName");
        Objects.requireNonNull(versionNameReference, "versionNameReference");
        Objects.requireNonNull(minSdk, "minSdk");
        Objects.requireNonNull(minSdkReference, "minSdkReference");
        Objects.requireNonNull(targetSdk, "targetSdk");
        Objects.requireNonNull(targetSdkReference, "targetSdkReference");
        permissions = List.copyOf(permissions);
        declaredPermissions = List.copyOf(declaredPermissions);
        components = List.copyOf(components);
        Objects.requireNonNull(launcherActivity, "launcherActivity");
        Objects.requireNonNull(debuggable, "debuggable");
        Objects.requireNonNull(debuggableReference, "debuggableReference");
        Objects.requireNonNull(allowBackup, "allowBackup");
        Objects.requireNonNull(allowBackupReference, "allowBackupReference");
    }

    /**
     * The components of one kind, in manifest order.
     *
     * @param kind the kind to keep
     * @return the components
     */
    public List<Component> componentsOf(ComponentKind kind) {
        return components.stream().filter(c -> c.kind() == kind).toList();
    }

    /**
     * The {@code android:protectionLevel} of a permission the app declares, as the raw Android
     * value (see {@link DeclaredPermission}).
     *
     * @param name a permission name
     * @return the level, or empty when the app does not declare this permission (for example a
     *     platform permission), or when its level is a resource reference that could not be
     *     resolved
     */
    public OptionalInt declaredPermissionProtectionLevel(String name) {
        for (DeclaredPermission p : declaredPermissions) {
            if (p.name().equals(name) && p.protectionLevel().isPresent()) {
                return p.protectionLevel();
            }
        }
        return OptionalInt.empty();
    }

    /**
     * Whether the app declares this permission with a protection level given by a resource
     * reference whose value is not the same in every configuration (see {@link
     * DeclaredPermission}).
     *
     * @param name a permission name
     * @return true for such a level; false for any other, and for a permission the app does not
     *     declare
     */
    public boolean declaredPermissionProtectionLevelVaries(String name) {
        for (DeclaredPermission p : declaredPermissions) {
            if (p.name().equals(name)) {
                return p.protectionLevelVaries();
            }
        }
        return false;
    }
}
