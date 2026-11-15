package io.github.burogurama.dexpp;

import java.util.List;
import java.util.Objects;
import java.util.Optional;

/**
 * A summary of one class in a {@link Model}.
 *
 * @param descriptor the class descriptor
 * @param accessFlags the class's access flags
 * @param superclass the descriptor of the superclass, or empty for {@code java.lang.Object}, which
 *     has none
 * @param sourceFile the source file name recorded for the class, or empty
 * @param interfaces the descriptors of the interfaces the class implements directly, in
 *     declaration order; for an interface, the interfaces it extends
 * @param fieldCount the number of fields the class declares
 * @param methodCount the number of methods the class defines
 */
public record ClassInfo(
        String descriptor,
        AccessFlags accessFlags,
        Optional<String> superclass,
        Optional<String> sourceFile,
        List<String> interfaces,
        int fieldCount,
        int methodCount) {
    /** Checks the parts and makes the list of interfaces immutable. */
    public ClassInfo {
        Objects.requireNonNull(descriptor, "descriptor");
        Objects.requireNonNull(accessFlags, "accessFlags");
        Objects.requireNonNull(superclass, "superclass");
        Objects.requireNonNull(sourceFile, "sourceFile");
        interfaces = List.copyOf(interfaces);
    }
}
