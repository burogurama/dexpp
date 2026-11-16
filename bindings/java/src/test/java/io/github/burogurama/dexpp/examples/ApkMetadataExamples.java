package io.github.burogurama.dexpp.examples;

import io.github.burogurama.dexpp.AnalysisContext;
import io.github.burogurama.dexpp.Apk;
import io.github.burogurama.dexpp.Certificate;
import io.github.burogurama.dexpp.Component;
import io.github.burogurama.dexpp.ComponentKind;
import io.github.burogurama.dexpp.DeclaredPermission;
import io.github.burogurama.dexpp.DexClass;
import io.github.burogurama.dexpp.InvalidApkException;
import io.github.burogurama.dexpp.Manifest;
import io.github.burogurama.dexpp.ResourceName;
import io.github.burogurama.dexpp.ResourceTable;
import io.github.burogurama.dexpp.SigningInfo;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.OptionalInt;

/**
 * The examples of the guide "APK metadata". ExamplesTest runs them. Code outside the markers is not
 * shown: it sets up the variables that the earlier sections of the guide made.
 */
final class ApkMetadataExamples {
    private ApkMetadataExamples() {}

    static void manifest() throws IOException {
        // --8<-- [start:manifest]
        try (Apk apk = Apk.open(Path.of("../../tests/data/sample.apk"))) {
            Manifest manifest = apk.manifest().orElseThrow();

            System.out.println(manifest.packageName());
            // An attribute the manifest may leave out is an Optional, empty when it is not set.
            if (manifest.versionCode().isPresent() && manifest.versionName().isPresent()) {
                System.out.println("version: " + manifest.versionCode().getAsLong() + " "
                        + manifest.versionName().get());
            }
            if (manifest.minSdk().isPresent() && manifest.targetSdk().isPresent()) {
                System.out.println("sdk: " + manifest.minSdk().getAsLong() + " to "
                        + manifest.targetSdk().getAsLong());
            }
            System.out.println("uses: " + manifest.permissions());
            for (DeclaredPermission permission : manifest.declaredPermissions()) {
                System.out.println("declares: " + permission.name());
            }
            if (manifest.debuggable().isPresent() && manifest.allowBackup().isPresent()) {
                System.out.println("debuggable: " + manifest.debuggable().get()
                        + " allow backup: " + manifest.allowBackup().get());
            }
        }
        // --8<-- [end:manifest]
    }

    static void components() throws IOException {
        try (Apk apk = Apk.open(Path.of("../../tests/data/sample.apk"))) {
            Manifest manifest = apk.manifest().orElseThrow();
            // --8<-- [start:components]
            for (Component component : manifest.components()) {
                String state = component.isExported() ? "exported" : "private";
                System.out.println(component.kind() + " " + component.name() + " " + state);
            }

            List<Component> activities = manifest.componentsOf(ComponentKind.ACTIVITY);
            Component launcher = activities.get(0);
            System.out.println(launcher.intentFilters().get(0).actions());
            // --8<-- [end:components]
        }
    }

    static void reachable() throws IOException {
        // --8<-- [start:reachable]
        try (Apk guardedApk = Apk.open(Path.of("../../tests/data/reachable.apk"))) {
            Manifest guarded = guardedApk.manifest().orElseThrow();

            for (Component component : guarded.components()) {
                if (component.isExported() && !component.isReachable()) {
                    String permission = component.permission().orElseThrow();
                    OptionalInt level = guarded.declaredPermissionProtectionLevel(permission);
                    System.out.printf("%s is exported, but guarded by %s (level %d)%n",
                            component.name(), permission, level.getAsInt());
                }
            }

            long reachable = guarded.components().stream().filter(Component::isReachable).count();
            System.out.println(reachable + " of " + guarded.components().size()
                    + " components are reachable");
        }
        // --8<-- [end:reachable]
    }

    static void resources() throws IOException {
        try (Apk apk = Apk.open(Path.of("../../tests/data/sample.apk"))) {
            // --8<-- [start:resources]
            ResourceTable table = apk.resources().orElseThrow();

            int appName = table.idOf("string", "app_name").orElseThrow();
            ResourceName name = table.nameOf(appName).orElseThrow();

            System.out.printf("%#x %s %s %s%n",
                    appName, name.packageName(), name.type(), name.entry());
            System.out.println(table.resolveString(appName));
            System.out.println(apk.resolveString(appName));
            // --8<-- [end:resources]
        }
    }

    static void signing() throws IOException {
        // --8<-- [start:signing]
        try (Apk signedApk = Apk.open(Path.of("../../tests/data/signed.apk"))) {
            SigningInfo signing = signedApk.signing();

            System.out.println("signed: " + signing.isSigned());
            System.out.println("v1: " + signing.v1() + " v2: " + signing.v2()
                    + " v3: " + signing.v3());
            for (Certificate certificate : signing.certificates()) {
                System.out.println(certificate.subject());
                System.out.println("valid " + certificate.notBefore()
                        + " to " + certificate.notAfter());
                System.out.println("sha256 " + certificate.sha256Hex());
            }
        }
        // --8<-- [end:signing]
    }

    static void entries() throws IOException {
        try (Apk apk = Apk.open(Path.of("../../tests/data/sample.apk"))) {
            // --8<-- [start:entries]
            List<String> entries = apk.entries();
            System.out.println(entries);

            byte[] classesDex = apk.read("classes.dex").orElseThrow();
            System.out.println(classesDex.length + " bytes of classes.dex");
            // --8<-- [end:entries]
        }
    }

    static void memory() throws IOException {
        // --8<-- [start:memory]
        // The bytes can come from anywhere: a download, an upload, a database.
        byte[] data = Files.readAllBytes(Path.of("../../tests/data/sample.apk"));

        try (Apk inMemory = Apk.fromBytes(data, "sample.apk")) {
            System.out.println(inMemory.packageName().orElseThrow());

            // analysis(1) keeps all the work on the calling thread.
            try (AnalysisContext code = inMemory.analysis(1)) {
                List<String> descriptors = new ArrayList<>();
                for (DexClass cls : code.classes()) {
                    descriptors.add(cls.descriptor());
                }
                System.out.println("classes: " + descriptors);
            }
        }

        // The label starts the error messages, where Apk.open puts the path.
        byte[] notAnApk = "not an apk".getBytes(StandardCharsets.US_ASCII);
        try (Apk broken = Apk.fromBytes(notAnApk, "upload.apk")) {
            System.out.println(broken.entries());
        } catch (InvalidApkException e) {
            System.out.println("InvalidApkException: " + e.getMessage());
        }
        // --8<-- [end:memory]
    }
}
