package io.github.burogurama.dexpp;

import static io.github.burogurama.dexpp.TestData.data;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.OptionalInt;
import java.util.OptionalLong;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/** The APK: archive entries, the manifest, resources and signing. */
class ApkTest {
    private static Apk apk(String name) throws IOException {
        return Apk.open(data(name));
    }

    @Test
    void archiveEntries() throws IOException {
        try (Apk apk = apk("sample.apk")) {
            assertTrue(apk.entries().contains("classes.dex"));
            byte[] dex = apk.read("classes.dex").orElseThrow();
            assertArrayEquals("dex\n".getBytes(StandardCharsets.US_ASCII), Arrays.copyOf(dex, 4));
            assertEquals(Optional.empty(), apk.read("nope"));
        }
    }

    @Test
    void manifest() throws IOException {
        try (Apk apk = apk("sample.apk")) {
            Manifest m = apk.manifest().orElseThrow();
            assertEquals(Optional.of("com.example.dexpp"), apk.packageName());
            assertEquals("com.example.dexpp", m.packageName());
            assertEquals(OptionalLong.of(7), m.versionCode());
            assertEquals(Optional.of("1.2.3"), m.versionName());
            assertEquals(OptionalLong.of(21), m.minSdk());
            assertEquals(OptionalLong.of(34), m.targetSdk());
            assertTrue(m.permissions().contains("android.permission.INTERNET"));
            assertEquals(1, m.declaredPermissions().size());
            assertEquals("com.example.dexpp.CUSTOM", m.declaredPermissions().get(0).name());
            assertEquals(Optional.of(true), m.debuggable());
            assertEquals(Optional.of(false), m.allowBackup());
            assertEquals(Optional.of("com.example.dexpp.MainActivity"), m.launcherActivity());

            List<Component> activities = m.componentsOf(ComponentKind.ACTIVITY);
            assertEquals(1, activities.size());
            Component main = activities.get(0);
            assertTrue(main.isExported());
            assertEquals(ComponentKind.ACTIVITY, main.kind());
            List<IntentFilter> filters = main.intentFilters();
            assertFalse(filters.isEmpty());
            assertTrue(filters.get(0).isLauncher());
            assertTrue(filters.get(0).hasAction("android.intent.action.MAIN"));
            assertFalse(filters.get(0).hasAction("android.intent.action.VIEW"));
            assertTrue(filters.get(0).categories().contains("android.intent.category.LAUNCHER"));

            Component service =
                    m.components().stream()
                            .filter(c -> c.name().endsWith("SyncService"))
                            .findFirst()
                            .orElseThrow();
            assertFalse(service.isExported());
            assertEquals(ComponentKind.SERVICE, service.kind());
            int total = 0;
            for (ComponentKind kind : ComponentKind.values()) {
                total += m.componentsOf(kind).size();
            }
            assertEquals(m.components().size(), total);
        }
    }

    @Test
    void reachability() throws IOException {
        try (Apk apk = apk("reachable.apk")) {
            Manifest m = apk.manifest().orElseThrow();
            Map<String, Integer> levels =
                    Map.of(
                            "com.dexpp.reach.P_NORMAL",
                            0,
                            "com.dexpp.reach.P_DANGEROUS",
                            1,
                            "com.dexpp.reach.P_SIGNATURE",
                            2);
            for (Map.Entry<String, Integer> e : levels.entrySet()) {
                assertEquals(
                        OptionalInt.of(e.getValue()),
                        m.declaredPermissionProtectionLevel(e.getKey()));
            }
            // An undeclared permission, such as a platform one, has no known level.
            assertEquals(
                    OptionalInt.empty(),
                    m.declaredPermissionProtectionLevel("android.permission.DUMP"));

            Map<String, Component> byName = new HashMap<>();
            for (Component c : m.components()) {
                byName.put(c.name().substring(c.name().lastIndexOf('.') + 1), c);
            }
            // Exported and unguarded, or guarded by a normal, dangerous or
            // undeclared permission: reachable.
            for (String name :
                    List.of(
                            "OpenActivity",
                            "NormalActivity",
                            "DangerousActivity",
                            "PlatformPermActivity",
                            "OpenReceiver")) {
                assertTrue(byName.get(name).isReachable(), name);
            }
            // Exported, but guarded by a signature permission the app declares.
            Component sig = byName.get("SigActivity");
            assertTrue(sig.isExported());
            assertTrue(sig.guardedBySignaturePermission());
            assertFalse(sig.isReachable());
            assertTrue(sig.permission().isPresent());
            Component sigService = byName.get("SigService");
            assertTrue(sigService.isExported());
            assertFalse(sigService.isReachable());
            // Not exported: not reachable.
            Component hidden = byName.get("HiddenActivity");
            assertFalse(hidden.isExported());
            assertFalse(hidden.isReachable());
            assertEquals(
                    Optional.of(false), hidden.exported(), "an explicit android:exported=false");

            long exported =
                    m.componentsOf(ComponentKind.ACTIVITY).stream()
                            .filter(Component::isExported)
                            .count();
            long reachable =
                    m.componentsOf(ComponentKind.ACTIVITY).stream()
                            .filter(Component::isReachable)
                            .count();
            assertEquals(5, exported);
            assertEquals(4, reachable);
        }
    }

    @Test
    void resources() throws IOException {
        try (Apk apk = apk("sample.apk")) {
            ResourceTable rt = apk.resources().orElseThrow();
            assertFalse(rt.isEmpty());
            int appName = rt.idOf("string", "app_name").orElseThrow();
            assertEquals(0x7f, appName >>> 24);
            assertEquals(Optional.of("Dexpp Sample"), rt.resolveString(appName));
            ResourceName want = new ResourceName("com.example.dexpp", "string", "app_name");
            assertEquals(Optional.of(want), rt.nameOf(appName));
            assertEquals("com.example.dexpp:string/app_name", want.toString());
            assertEquals(OptionalInt.empty(), rt.idOf("string", "nope"));
            assertEquals(Optional.empty(), rt.resolveString(0x7f999999));
            assertEquals(Optional.empty(), rt.nameOf(0x7f999999));
            // The convenience on the Apk resolves through the same table.
            assertEquals(Optional.of("Dexpp Sample"), apk.resolveString(appName));
            assertEquals(Optional.empty(), apk.resolveString(0x7f999999));
        }
    }

    @Test
    void anApkWithoutAManifestOrResources() throws IOException {
        // app.apk is a ZIP of DEX files only.
        try (Apk apk = apk("app.apk")) {
            assertEquals(Optional.empty(), apk.manifest());
            assertEquals(Optional.empty(), apk.packageName());
            assertEquals(Optional.empty(), apk.resources());
            assertEquals(Optional.empty(), apk.resolveString(0x7f010000));
            assertFalse(apk.signing().isSigned());
        }
    }

    @Test
    void signing() throws IOException {
        try (Apk apk = apk("signed.apk")) {
            SigningInfo s = apk.signing();
            assertTrue(s.isSigned());
            assertTrue(s.v1() && s.v2() && s.v3());
            assertEquals(1, s.certificates().size());
            Certificate c = s.certificates().get(0);
            assertEquals("C=MA, O=DexppOrg, CN=Dexpp Test", c.subject());
            assertEquals(c.subject(), c.issuer());
            assertTrue(c.selfSigned());
            assertEquals("ba51e610667560a3", c.serialHex());
            assertEquals(
                    "6c9647205d1ae141f9104587153cb1c71c5da9251335da1a1977de500c6ef599",
                    c.sha256Hex());
            assertEquals(40, c.sha1Hex().length());
            assertTrue(c.der().length > 0);
            assertEquals(0x30, c.der()[0]);
            assertEquals("2026-01-01T00:00:00Z".length(), c.notBefore().length());
            assertTrue(c.notBefore().compareTo(c.notAfter()) < 0);
            // der() is a copy, and equals compares content.
            c.der()[0] = 0;
            assertEquals(0x30, c.der()[0]);
            assertEquals(c, apk.signing().certificates().get(0));
        }
        try (Apk unsigned = apk("sample.apk")) {
            SigningInfo s = unsigned.signing();
            assertFalse(s.isSigned());
            assertEquals(List.of(), s.certificates());
        }
    }

    @Test
    void apkAnalysis() throws IOException {
        AnalysisContext ctx;
        try (Apk apk = apk("sample.apk")) {
            ctx = apk.analysis();
        }
        try (ctx) { // the context stays valid after the Apk is closed
            assertTrue(ctx.findClass("LTestDex;").isPresent());
        }
        try (Apk noDex = apk("nodex.apk")) {
            assertThrows(InvalidApkException.class, noDex::analysis);
        }
    }

    @Test
    void readsAnEmptyEntry(@TempDir Path dir) throws IOException {
        // A one-entry ZIP whose entry is empty: read gives an empty array, not
        // an empty Optional.
        ByteArrayOutputStream buffer = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(buffer)) {
            ZipEntry entry = new ZipEntry("e");
            entry.setMethod(ZipEntry.STORED);
            entry.setSize(0);
            entry.setCrc(new CRC32().getValue());
            zip.putNextEntry(entry);
            zip.closeEntry();
        }
        Path path = dir.resolve("empty-entry.zip");
        Files.write(path, buffer.toByteArray());
        try (Apk apk = Apk.open(path)) {
            assertEquals(List.of("e"), apk.entries());
            assertArrayEquals(new byte[0], apk.read("e").orElseThrow());
        }
    }
}
