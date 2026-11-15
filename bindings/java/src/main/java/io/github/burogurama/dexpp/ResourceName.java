package io.github.burogurama.dexpp;

import java.util.Objects;

/**
 * The full name of a resource: {@code package:type/entry}.
 *
 * @param packageName the package, such as {@code "com.example.app"}
 * @param type the type, such as {@code "string"} or {@code "drawable"}
 * @param entry the entry, such as {@code "app_name"}
 */
public record ResourceName(String packageName, String type, String entry) {
    /** Checks that every part is given. */
    public ResourceName {
        Objects.requireNonNull(packageName, "packageName");
        Objects.requireNonNull(type, "type");
        Objects.requireNonNull(entry, "entry");
    }

    /** Writes the name as {@code "package:type/entry"}. */
    @Override
    public String toString() {
        return packageName + ":" + type + "/" + entry;
    }
}
