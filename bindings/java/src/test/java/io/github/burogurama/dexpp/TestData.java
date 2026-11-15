package io.github.burogurama.dexpp;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.file.Path;

/**
 * Paths of the repository, and small helpers the tests share. Maven runs the tests in
 * bindings/java, so the repository root is two folders up.
 */
final class TestData {
    private TestData() {}

    /** The root of the repository. */
    static final Path ROOT = Path.of("..", "..");

    /** A fixture in tests/data. */
    static Path data(String name) {
        return ROOT.resolve("tests").resolve("data").resolve(name);
    }

    /** Loads a fixture DEX file. */
    static AnalysisContext dex(String name) {
        try {
            return AnalysisContext.fromDex(data(name));
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    /** The class with this descriptor; fails the test when there is none. */
    static DexClass cls(AnalysisContext ctx, String descriptor) {
        return ctx.findClass(descriptor)
                .orElseThrow(() -> new AssertionError("no class " + descriptor));
    }

    /** The first method with this name; fails the test when there is none. */
    static DexMethod method(DexClass cls, String name) {
        for (DexMethod m : cls.methods()) {
            if (m.name().equals(name)) {
                return m;
            }
        }
        throw new AssertionError(cls.name() + " has no method " + name);
    }

    /** The field with this name; fails the test when there is none. */
    static DexField field(DexClass cls, String name) {
        for (DexField f : cls.fields()) {
            if (f.name().equals(name)) {
                return f;
            }
        }
        throw new AssertionError(cls.name() + " has no field " + name);
    }
}
