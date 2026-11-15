package io.github.burogurama.dexpp;

import java.util.Objects;

/**
 * The identity of a method: its class, its name and its prototype. It is a plain value, so it can
 * be compared with {@code equals} and used as a map key. An identity also exists for a method that
 * is not in the loaded files, such as a framework method.
 *
 * @param classDescriptor the class, such as {@code "Ljava/lang/Object;"}
 * @param name the method name, such as {@code "toString"}; a constructor is {@code "<init>"}
 * @param proto the prototype, such as {@code "()Ljava/lang/String;"}
 */
public record MethodId(String classDescriptor, String name, String proto) implements MemberId {
    /** Checks that every part is given. */
    public MethodId {
        Objects.requireNonNull(classDescriptor, "classDescriptor");
        Objects.requireNonNull(name, "name");
        Objects.requireNonNull(proto, "proto");
    }

    /**
     * Writes the identity as {@code "Lcls;->name(proto)"}, such as {@code
     * "Ljava/lang/Object;->toString()Ljava/lang/String;"}.
     */
    @Override
    public String toString() {
        return classDescriptor + "->" + name + proto;
    }
}
