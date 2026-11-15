package io.github.burogurama.dexpp;

import java.util.Objects;

/**
 * The identity of a field: its class, its name and its type. It is a plain value, so it can be
 * compared with {@code equals} and used as a map key.
 *
 * @param classDescriptor the class, such as {@code "Ljava/lang/System;"}
 * @param name the field name, such as {@code "out"}
 * @param type the type descriptor, such as {@code "Ljava/io/PrintStream;"}
 */
public record FieldId(String classDescriptor, String name, String type) implements MemberId {
    /** Checks that every part is given. */
    public FieldId {
        Objects.requireNonNull(classDescriptor, "classDescriptor");
        Objects.requireNonNull(name, "name");
        Objects.requireNonNull(type, "type");
    }

    /**
     * Writes the identity as {@code "Lcls;->name:type"}, such as {@code
     * "Ljava/lang/System;->out:Ljava/io/PrintStream;"}.
     */
    @Override
    public String toString() {
        return classDescriptor + "->" + name + ":" + type;
    }
}
