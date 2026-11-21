package io.github.burogurama.dexpp;

import java.util.Objects;
import java.util.Optional;

/**
 * A {@code <path-permission>} of a content provider: permissions that also give access to the
 * provider's paths that match.
 *
 * <p>A caller that holds the path's read or write permission can read or write the matching paths,
 * even without the provider's own permission. So a path permission widens access to a provider that
 * has a permission of its own. (When the provider has none, a path permission closes the matching
 * paths to callers without it, but every other path stays open.) Android drops a {@code
 * <path-permission>} that gives neither a read nor a write permission, or no path; dexpp leaves
 * those out too.
 *
 * @param path the path or the pattern. It is empty when {@code pathUnknown} is true.
 * @param match the attribute the path comes from, which says how Android matches it: the first that
 *     gives a value, in the order {@code android:pathAdvancedPattern}, {@code pathPattern}, {@code
 *     pathPrefix}, {@code pathSuffix}, {@code path}
 * @param pathUnknown true when dexpp cannot tell the path: in that order, an attribute whose value
 *     dexpp cannot tell comes before any that gives a value. {@code match} is then that attribute.
 * @param readPermission the permission that lets a caller read the matching paths: {@code
 *     android:readPermission}, or the element's {@code android:permission} when that gives none. It
 *     never falls back to the provider's permissions. It is empty when neither gives one, or when it
 *     is unknown ({@code readPermissionUnknown}).
 * @param readPermissionUnknown true when dexpp cannot tell {@code readPermission}
 * @param writePermission the permission that lets a caller write the matching paths: {@code
 *     android:writePermission}, or the element's {@code android:permission}. The same rules as
 *     {@code readPermission}.
 * @param writePermissionUnknown true when dexpp cannot tell {@code writePermission}
 */
public record PathPermission(
        String path,
        PathMatch match,
        boolean pathUnknown,
        Optional<String> readPermission,
        boolean readPermissionUnknown,
        Optional<String> writePermission,
        boolean writePermissionUnknown) {
    /** Checks the parts. */
    public PathPermission {
        Objects.requireNonNull(path, "path");
        Objects.requireNonNull(match, "match");
        Objects.requireNonNull(readPermission, "readPermission");
        Objects.requireNonNull(writePermission, "writePermission");
    }
}
