package io.github.burogurama.dexpp;

import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.OptionalInt;

/**
 * An activity, a service, a broadcast receiver or a content provider declared in the manifest.
 *
 * @param name the component's class name with its package, such as {@code
 *     "com.example.MainActivity"}. A short name in the manifest, such as {@code ".MainActivity"},
 *     is completed with the app's package. It is empty when the attribute is missing, or when it is
 *     a reference that gives no single value (then {@code nameReference} is set).
 * @param nameReference the resource id when {@code android:name} is written as a reference, such
 *     as {@code "@string/main_activity"}, whether it was resolved or not. It is empty for a plain
 *     value or a missing attribute.
 * @param kind the kind of component
 * @param exported the {@code android:exported} attribute: the value the manifest writes, or the
 *     value of the resource it refers to in the default configuration. Text reads as Android reads
 *     it: only {@code "1"}, {@code "true"} and {@code "TRUE"} are true. It is empty when the
 *     attribute is missing, or when it is a reference that could not be resolved (then {@code
 *     exportedReference} is set). {@code isExported} gives the effective answer.
 * @param exportedReference the resource id when {@code android:exported} is written as a
 *     reference, such as {@code "@bool/is_exported"} (or a theme attribute, {@code "?attr/name"}),
 *     whether it was resolved or not. It is empty for a plain value or a missing attribute. An empty
 *     {@code exported} with a set {@code exportedReference} means the value is unknown.
 * @param exportedVaries true when {@code android:exported} is a reference whose value is not the
 *     same in every resource configuration: for example false in {@code res/values} and true in
 *     {@code res/values-v31}, so the component is exported on Android 12 and newer. {@code
 *     exported} holds the default configuration's value, and {@code isExported} is true.
 * @param permission the permission a caller must hold to use the component, or empty. It is also
 *     empty when {@code android:permission} is a reference that gives no single value (then {@code
 *     permissionReference} is set): Android ignores a string that changes with the language, the
 *     screen or another configuration, and a string that changes only with the Android version
 *     depends on the device.
 * @param permissionReference the resource id when {@code android:permission} is written as a
 *     reference, such as {@code "@string/guard"}, whether it was resolved or not. It is empty for a
 *     plain value or a missing attribute.
 * @param intentFilters the intent filters
 * @param guardedBySignaturePermission true when {@code permission} is a permission this app
 *     declares with the {@code signature} or {@code signatureOrSystem} protection level. Only apps
 *     signed with the same key can hold such a permission.
 * @param isExported whether other apps can address the component: the {@code exported} attribute
 *     when the manifest sets it, otherwise true when the component has an intent filter. When the
 *     attribute is a reference whose value differs between configurations ({@code
 *     exportedVaries}), this is true. When the attribute is a reference that could not be
 *     resolved, its value is unknown and this is true: the component may be exported.
 * @param isReachable whether an app from another developer can reach the component: it is exported,
 *     and it is not guarded by a signature-level permission that this app declares. A component
 *     guarded by a normal or dangerous permission stays reachable, because any app can request
 *     those. So does a component guarded by a permission the manifest does not declare, because its
 *     protection level is not known.
 */
public record Component(
        String name,
        OptionalInt nameReference,
        ComponentKind kind,
        Optional<Boolean> exported,
        OptionalInt exportedReference,
        boolean exportedVaries,
        Optional<String> permission,
        OptionalInt permissionReference,
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
        intentFilters = List.copyOf(intentFilters);
    }
}
