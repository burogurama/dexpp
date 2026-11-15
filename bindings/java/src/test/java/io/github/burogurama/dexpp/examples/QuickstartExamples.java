package io.github.burogurama.dexpp.examples;

import io.github.burogurama.dexpp.AnalysisContext;
import io.github.burogurama.dexpp.Apk;
import io.github.burogurama.dexpp.Certificate;
import io.github.burogurama.dexpp.DexClass;
import io.github.burogurama.dexpp.DexMethod;
import io.github.burogurama.dexpp.Manifest;

import java.io.IOException;
import java.nio.file.Path;
import java.util.List;

/** The example of the documentation's home page. ExamplesTest runs it. */
final class QuickstartExamples {
    private QuickstartExamples() {}

    /** Open an APK, read its metadata, then look at its code. */
    static void quickstart() throws IOException {
        // --8<-- [start:quickstart]
        try (Apk apk = Apk.open(Path.of("../../tests/data/signed.apk"))) {
            // Package metadata comes from the manifest. It is empty when the APK has none.
            Manifest manifest = apk.manifest().orElseThrow();
            System.out.println("package: " + manifest.packageName());
            manifest.versionName()
                    .ifPresent((String version) -> System.out.println("version: " + version));
            System.out.println("permissions: " + manifest.permissions());
            manifest.launcherActivity()
                    .ifPresent((String launcher) -> System.out.println("launcher: " + launcher));

            // Who signed it.
            for (Certificate certificate : apk.signing().certificates()) {
                String fingerprint = certificate.sha256Hex().substring(0, 16) + "...";
                System.out.println("signed by: " + certificate.subject() + " " + fingerprint);
            }

            // The code: every classes*.dex of the APK in one analysis context.
            try (AnalysisContext ctx = apk.analysis()) {
                for (DexClass cls : ctx.classes()) {
                    List<String> methodNames = cls.methods().stream().map(DexMethod::name).toList();
                    System.out.println(cls.descriptor() + " " + methodNames);
                }
            }
        }
        // --8<-- [end:quickstart]
    }
}
