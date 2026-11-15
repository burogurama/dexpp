package io.github.burogurama.dexpp;

import java.util.List;
import java.util.Objects;
import java.util.Optional;

/**
 * An activity, a service, a broadcast receiver or a content provider declared in the manifest.
 *
 * @param name the component's class name with its package, such as {@code
 *     "com.example.MainActivity"}. A short name in the manifest, such as {@code ".MainActivity"},
 *     is completed with the app's package.
 * @param kind the kind of component
 * @param exported the {@code android:exported} attribute as the manifest writes it, or empty when
 *     the attribute is missing; {@code isExported} gives the effective answer
 * @param permission the permission a caller must hold to use the component, or empty
 * @param intentFilters the intent filters
 * @param guardedBySignaturePermission true when {@code permission} is a permission this app
 *     declares with the {@code signature} or {@code signatureOrSystem} protection level. Only apps
 *     signed with the same key can hold such a permission.
 * @param isExported whether other apps can address the component: the {@code exported} attribute
 *     when the manifest sets it, otherwise true when the component has an intent filter
 * @param isReachable whether an app from another developer can reach the component: it is exported,
 *     and it is not guarded by a signature-level permission that this app declares. A component
 *     guarded by a normal or dangerous permission stays reachable, because any app can request
 *     those. So does a component guarded by a permission the manifest does not declare, because its
 *     protection level is not known.
 */
public record Component(
        String name,
        ComponentKind kind,
        Optional<Boolean> exported,
        Optional<String> permission,
        List<IntentFilter> intentFilters,
        boolean guardedBySignaturePermission,
        boolean isExported,
        boolean isReachable) {
    /** Checks the parts, and copies the filters into an immutable list. */
    public Component {
        Objects.requireNonNull(name, "name");
        Objects.requireNonNull(kind, "kind");
        Objects.requireNonNull(exported, "exported");
        Objects.requireNonNull(permission, "permission");
        intentFilters = List.copyOf(intentFilters);
    }
}
