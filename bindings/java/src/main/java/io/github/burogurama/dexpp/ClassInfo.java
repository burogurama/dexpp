package io.github.burogurama.dexpp;

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
 * @param interfaceCount the number of interfaces the class implements directly
 * @param fieldCount the number of fields the class declares
 * @param methodCount the number of methods the class defines
 */
public record ClassInfo(
        String descriptor,
        AccessFlags accessFlags,
        Optional<String> superclass,
        Optional<String> sourceFile,
        int interfaceCount,
        int fieldCount,
        int methodCount) {
    /** Checks the parts. */
    public ClassInfo {
        Objects.requireNonNull(descriptor, "descriptor");
        Objects.requireNonNull(accessFlags, "accessFlags");
        Objects.requireNonNull(superclass, "superclass");
        Objects.requireNonNull(sourceFile, "sourceFile");
    }
}
