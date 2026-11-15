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
 * @param packageName the {@code package} attribute of {@code <manifest>}
 * @param versionCode {@code android:versionCode}
 * @param versionName {@code android:versionName}
 * @param minSdk {@code android:minSdkVersion} of {@code <uses-sdk>}
 * @param targetSdk {@code android:targetSdkVersion} of {@code <uses-sdk>}
 * @param permissions the permissions the app asks for: the name of each {@code <uses-permission>}
 * @param declaredPermissions the permissions the app itself declares, in manifest order
 * @param components every activity, service, receiver and provider, in manifest order
 * @param launcherActivity the name of the activity the launcher starts
 * @param debuggable {@code android:debuggable} of {@code <application>}
 * @param allowBackup {@code android:allowBackup} of {@code <application>}
 */
public record Manifest(
        String packageName,
        OptionalLong versionCode,
        Optional<String> versionName,
        OptionalLong minSdk,
        OptionalLong targetSdk,
        List<String> permissions,
        List<DeclaredPermission> declaredPermissions,
        List<Component> components,
        Optional<String> launcherActivity,
        Optional<Boolean> debuggable,
        Optional<Boolean> allowBackup) {
    /** Checks the parts, and copies the lists into immutable lists. */
    public Manifest {
        Objects.requireNonNull(packageName, "packageName");
        Objects.requireNonNull(versionCode, "versionCode");
        Objects.requireNonNull(versionName, "versionName");
        Objects.requireNonNull(minSdk, "minSdk");
        Objects.requireNonNull(targetSdk, "targetSdk");
        permissions = List.copyOf(permissions);
        declaredPermissions = List.copyOf(declaredPermissions);
        components = List.copyOf(components);
        Objects.requireNonNull(launcherActivity, "launcherActivity");
        Objects.requireNonNull(debuggable, "debuggable");
        Objects.requireNonNull(allowBackup, "allowBackup");
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
     * @return the level, or empty when the app does not declare this permission, for example for a
     *     platform permission
     */
    public OptionalInt declaredPermissionProtectionLevel(String name) {
        for (DeclaredPermission p : declaredPermissions) {
            if (p.name().equals(name) && p.protectionLevel().isPresent()) {
                return p.protectionLevel();
            }
        }
        return OptionalInt.empty();
    }
}
