package io.github.burogurama.dexpp;

import java.util.List;
import java.util.Objects;

/**
 * The call sites of one called method, as {@link Xrefs#methodRefsByName} finds them.
 *
 * @param target the called method
 * @param sites its call sites
 */
public record MethodRefGroup(MethodId target, List<MethodXref> sites) {
    /** Checks the parts, and copies the sites into an immutable list. */
    public MethodRefGroup {
        Objects.requireNonNull(target, "target");
        sites = List.copyOf(sites);
    }
}
