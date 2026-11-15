package io.github.burogurama.dexpp.examples;

import io.github.burogurama.dexpp.AnalysisContext;
import io.github.burogurama.dexpp.Apk;
import io.github.burogurama.dexpp.DexppException;
import io.github.burogurama.dexpp.InvalidApkException;
import io.github.burogurama.dexpp.Manifest;

import java.io.IOException;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.util.List;
import java.util.Optional;

/**
 * The examples about errors, for the quick start, the concepts page and the Java reference page.
 * ExamplesTest runs them.
 */
final class ErrorsExamples {
    private ErrorsExamples() {}

    static void load() throws IOException {
        // --8<-- [start:load]
        for (String name : List.of("sample.apk", "missing.apk", "classes.dex")) {
            try (Apk apk = Apk.open(Path.of("../../tests/data", name))) {
                Optional<String> packageName = apk.manifest().map(Manifest::packageName);
                System.out.println(packageName.orElse("no manifest"));
            } catch (NoSuchFileException e) {
                System.out.println(name + " does not exist");
            } catch (InvalidApkException e) {
                System.out.println("not an APK: " + name + " (" + e.code() + ")");
            }
        }
        // --8<-- [end:load]
    }

    static void base() throws IOException {
        // --8<-- [start:base]
        Path notDex = Path.of("../../tests/data/TestDex.java");
        try (AnalysisContext ctx = AnalysisContext.fromDex(notDex)) {
            System.out.println(ctx.classes().size());
        } catch (DexppException e) {
            System.out.println(e.getClass().getSimpleName() + " - " + e.code());
        }
        // --8<-- [end:base]
    }
}
