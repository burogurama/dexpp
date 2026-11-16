package io.github.burogurama.dexpp;

import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.OptionalInt;

/**
 * An activity, a service, a broadcast receiver or a content provider declared in the manifest.
 *
 * <p>A {@code ...Reference} component holds the resource id when the attribute is written as a
 * reference, and is empty for a plain value or a missing attribute. When the value is empty and the
 * reference is set, the matching {@code ...Unknown} component tells why: true when dexpp cannot
 * tell the value (the reference cannot be resolved), false when Android reads the attribute as not
 * written (a null value such as {@code @null} or {@code @empty}, or a string that changes with the
 * configuration).
 *
 * @param name the component's class name with its package, such as {@code
 *     "com.example.MainActivity"}. A short name in the manifest, such as {@code ".MainActivity"},
 *     is completed with the app's package. It is empty when the attribute is missing, or when it is
 *     a reference that gives no value (then {@code nameReference} is set).
 * @param nameReference the resource id when {@code android:name} is written as a reference, such
 *     as {@code "@string/main_activity"}, whether it was resolved or not. It is empty for a plain
 *     value or a missing attribute.
 * @param nameUnknown true when {@code android:name} is a reference whose string dexpp cannot tell
 * @param kind the kind of component
 * @param exported the {@code android:exported} attribute: the value the manifest writes, or the
 *     value of the resource it refers to in the default configuration. Text reads as Android reads
 *     it: only {@code "1"}, {@code "true"} and {@code "TRUE"} are true. It is empty when the
 *     attribute is missing, when it is a null value (Android reads it as not written), or when its
 *     value is unknown ({@code exportedUnknown}). {@code isExported} gives the effective answer.
 * @param exportedReference the resource id when {@code android:exported} is written as a
 *     reference, such as {@code "@bool/is_exported"} (or a theme attribute, {@code "?attr/name"}),
 *     whether it was resolved or not. It is empty for a plain value or a missing attribute.
 * @param exportedUnknown true when {@code android:exported} is a reference whose value in the
 *     default configuration dexpp cannot tell. {@code exported} is then empty, and {@code
 *     isExported} is true.
 * @param exportedVaries true when {@code android:exported} is a reference that makes the component
 *     exported in some resource configurations and not in others: for example false in {@code
 *     res/values} and true in {@code res/values-v31}, so the component is exported on Android 12
 *     and newer. A configuration whose value is null uses the intent-filter rule. {@code exported}
 *     holds the default configuration's value, and {@code isExported} is true.
 * @param permission the permission a caller must hold to use the component, as Android enforces
 *     it: the component's {@code android:permission}, or, when the component gives none, the {@code
 *     android:permission} of {@code <application>} (then {@code permissionInherited} is true). The
 *     component gives none when the attribute is missing or Android reads it as not written: a null
 *     value, or a string that changes with the language, the screen or another configuration, which
 *     Android ignores. It is empty when neither gives one, or when it is unknown ({@code
 *     permissionUnknown}). For a provider, it is the permission {@code readPermission} and {@code
 *     writePermission} fall back to.
 * @param permissionReference the resource id when the component's own {@code android:permission}
 *     is written as a reference, such as {@code "@string/guard"}, whether it was resolved or not.
 *     It is empty for a plain value or a missing attribute.
 * @param permissionUnknown true when dexpp cannot tell {@code permission}: the component's own
 *     {@code android:permission} is a reference that cannot be resolved or a string that differs
 *     between Android versions, or the component gives none and the application's permission is
 *     unknown
 * @param permissionInherited true when {@code permission} comes from {@code <application>}: the
 *     component gives no permission of its own, and the application's gives one or is unknown
 * @param readPermission for a provider, the permission a caller needs to read from it: {@code
 *     android:readPermission}, or {@code permission} when that gives none. It is empty for the
 *     other kinds, when no permission applies, or when it is unknown ({@code
 *     readPermissionUnknown}).
 * @param readPermissionReference the resource id when the provider's {@code
 *     android:readPermission} is written as a reference
 * @param readPermissionUnknown true when dexpp cannot tell {@code readPermission}
 * @param writePermission for a provider, the permission a caller needs to write to it: {@code
 *     android:writePermission}, or {@code permission} when that gives none. The same rules as
 *     {@code readPermission}.
 * @param writePermissionReference the resource id when the provider's {@code
 *     android:writePermission} is written as a reference
 * @param writePermissionUnknown true when dexpp cannot tell {@code writePermission}
 * @param intentFilters the intent filters
 * @param guardedBySignaturePermission true when the permission Android enforces is a permission
 *     this app declares with the {@code signature} or {@code signatureOrSystem} protection level.
 *     Only apps signed with the same key can hold such a permission. For a provider, both {@code
 *     readPermission} and {@code writePermission} must be such a permission.
 * @param isExported whether other apps can address the component: the {@code exported} attribute
 *     when the manifest sets it, otherwise true when the component has an intent filter. A null
 *     value counts as not set. When the attribute is a reference whose value differs between
 *     configurations ({@code exportedVaries}), this is true. When the attribute is a reference
 *     whose value is unknown ({@code exportedUnknown}), this is true: the component may be
 *     exported.
 * @param isReachable whether an app from another developer can reach the component: it is exported,
 *     and it is not guarded by a signature-level permission that this app declares. A component
 *     guarded by a normal or dangerous permission stays reachable, because any app can request
 *     those. So does a component guarded by a permission the manifest does not declare, because its
 *     protection level is not known, and one whose permission is unknown.
 */
public record Component(
        String name,
        OptionalInt nameReference,
        boolean nameUnknown,
        ComponentKind kind,
        Optional<Boolean> exported,
        OptionalInt exportedReference,
        boolean exportedUnknown,
        boolean exportedVaries,
        Optional<String> permission,
        OptionalInt permissionReference,
        boolean permissionUnknown,
        boolean permissionInherited,
        Optional<String> readPermission,
        OptionalInt readPermissionReference,
        boolean readPermissionUnknown,
        Optional<String> writePermission,
        OptionalInt writePermissionReference,
        boolean writePermissionUnknown,
        List<IntentFilter> intentFilters,
        boolean guardedBySignaturePermission,
        boolean isExported,
        boolean isReachable) {
    /** Checks the parts, and copies the filters into an immutable list. */
    public Component {
        Objects.requireNonNull(name, "name");
        Objects.requireNonNull(nameReference, "nameReference");
        Objects.requireNonNull(kind, "kind");
        Objects.requireNonNull(exported, "exported");
        Objects.requireNonNull(exportedReference, "exportedReference");
        Objects.requireNonNull(permission, "permission");
        Objects.requireNonNull(permissionReference, "permissionReference");
        Objects.requireNonNull(readPermission, "readPermission");
        Objects.requireNonNull(readPermissionReference, "readPermissionReference");
        Objects.requireNonNull(writePermission, "writePermission");
        Objects.requireNonNull(writePermissionReference, "writePermissionReference");
        intentFilters = List.copyOf(intentFilters);
    }
}
