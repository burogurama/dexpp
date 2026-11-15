package io.github.burogurama.dexpp;

import static io.github.burogurama.dexpp.TestData.data;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.function.Executable;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.FileSystemException;
import java.nio.file.Files;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;

/**
 * The editable DEX model. shapes.dex holds an interface (LShape;), an abstract class implementing
 * it (LAbstractShape;), and two concrete subclasses (LCircle;, LRectangle;).
 */
class ModelTest {
    private static Model read(String name) throws IOException {
        return Model.readFile(data(name));
    }

    @Test
    void readAndClasses() throws IOException {
        try (Model m = read("shapes.dex")) {
            List<String> want = List.of("LShape;", "LAbstractShape;", "LCircle;", "LRectangle;");
            assertEquals(want, m.classDescriptors());
            assertEquals(want.size(), m.classCount());
            assertTrue(m.hasClass("LCircle;"));
            assertFalse(m.hasClass("Lno/such/Class;"));
        }
    }

    @Test
    void classInfo() throws IOException {
        try (Model m = read("shapes.dex")) {
            ClassInfo circle = m.classInfo("LCircle;").orElseThrow();
            assertEquals("LCircle;", circle.descriptor());
            assertTrue(circle.methodCount() > 0);
            assertEquals(Optional.of("LAbstractShape;"), circle.superclass());
            assertEquals(Optional.of("Shapes.java"), circle.sourceFile());
            assertEquals(List.of(), circle.interfaces());
            assertEquals(
                    List.of("LShape;"), m.classInfo("LAbstractShape;").orElseThrow().interfaces());
            // An interface's own superclass is java.lang.Object.
            ClassInfo shape = m.classInfo("LShape;").orElseThrow();
            assertEquals(Optional.of("Ljava/lang/Object;"), shape.superclass());
            assertTrue(shape.accessFlags().has(AccessFlags.INTERFACE));
            assertEquals(List.of(), shape.interfaces());
            assertEquals(Optional.empty(), m.classInfo("Lno/such/Class;"));
        }
    }

    @Test
    void writeRoundTrip() throws IOException {
        try (Model m = read("shapes.dex")) {
            byte[] out = m.write();
            assertTrue(out.length > 0);
            try (Model again = Model.read(out)) {
                assertEquals(m.classCount(), again.classCount());
                // write(read(write(m))) gives the same bytes.
                assertArrayEquals(out, again.write(true));
            }
            // Without debug information the file is smaller and still reads back.
            byte[] stripped = m.write(false);
            assertTrue(stripped.length < out.length);
            try (Model back = Model.read(stripped, false)) {
                assertEquals(m.classCount(), back.classCount());
            }
        }
    }

    @Test
    void splitOnePerClass() throws IOException {
        try (Model src = read("shapes.dex");
                Model work = src.copy()) {
            List<String> descriptors = src.classDescriptors();
            for (String d : descriptors) {
                byte[] out;
                try (Model one = new Model(src.version())) {
                    one.moveClass(work, d);
                    out = one.write();
                }
                try (Model part = Model.read(out)) {
                    assertEquals(List.of(d), part.classDescriptors());
                }
            }
            // The copy was drained; the original is untouched.
            assertEquals(0, work.classCount());
            assertEquals(descriptors.size(), src.classCount());
        }
    }

    @Test
    void copyRemoveAndErrors() throws IOException {
        try (Model src = read("shapes.dex");
                Model dst = new Model(src.version());
                Model empty = new Model(35)) {
            dst.copyClass(src, "LCircle;");
            assertTrue(src.hasClass("LCircle;"));
            assertTrue(dst.hasClass("LCircle;"));
            for (Executable change :
                    List.<Executable>of(
                            () -> dst.copyClass(src, "LCircle;"), // into a model that has it
                            () -> dst.moveClass(src, "LCircle;"),
                            () -> dst.copyClass(src, "Lno/such/Class;"), // a class the source lacks
                            () -> dst.moveClass(src, "Lno/such/Class;"),
                            () -> dst.moveClass(dst, "LCircle;"), // within one model
                            () -> dst.copyClass(empty, "LShape;"))) {
                ModelException e = assertThrows(ModelException.class, change);
                assertEquals(ErrorCode.MODEL, e.code());
                assertFalse(e.getMessage().isEmpty());
            }
            assertTrue(dst.removeClass("LCircle;"));
            assertFalse(dst.removeClass("LCircle;"));
            assertEquals(0, dst.classCount());
        }
    }

    @Test
    void renameTypes(@TempDir Path dir) throws IOException {
        try (Model m = read("shapes.dex")) {
            m.renameTypes(Map.of("LCircle;", "Lcom/x/Round;"));
            m.renameTypes(Map.of()); // an empty mapping changes nothing
            assertFalse(m.hasClass("LCircle;"));
            assertTrue(m.hasClass("Lcom/x/Round;"));
            // The rename reaches references too, and the model still writes.
            Path out = dir.resolve("out.dex");
            Files.write(out, m.write());
            try (AnalysisContext ctx = AnalysisContext.fromDex(out)) {
                assertTrue(ctx.findClass("Lcom/x/Round;").isPresent());
                assertEquals(Optional.empty(), ctx.findClass("LCircle;"));
            }
        }
    }

    @Test
    void validateAndStats() throws IOException {
        try (Model m = read("shapes.dex")) {
            assertEquals(List.of(), m.validate());
            PoolStats stats = m.poolStats();
            assertTrue(
                    stats.strings() > 0
                            && stats.types() > 0
                            && stats.protos() > 0
                            && stats.methods() > 0,
                    stats.toString());
            assertEquals(0, stats.methodHandles());
            assertEquals(0, stats.callSites());
            assertEquals(35, m.requiredVersion());
        }
    }

    @Test
    void newModelAndVersion() {
        try (Model m = new Model(39)) {
            assertEquals(39, m.version());
            assertEquals(0, m.classCount());
            assertEquals(List.of(), m.classDescriptors());
            m.setVersion(35);
            assertEquals(35, m.version());
            // An empty model writes a valid DEX file that reads back.
            try (Model again = Model.read(m.write())) {
                assertEquals(0, again.classCount());
            }
        }
    }

    @Test
    void writeFile(@TempDir Path dir) throws IOException {
        try (Model m = read("shapes.dex")) {
            Path path = dir.resolve("out.dex");
            m.writeFile(path);
            assertArrayEquals(m.write(), Files.readAllBytes(path));
            m.writeFile(path, false);
            assertArrayEquals(m.write(false), Files.readAllBytes(path));
        }
    }

    @Test
    void readErrors(@TempDir Path dir) throws IOException {
        assertThrows(
                InvalidDexException.class,
                () -> Model.read("not a dex".getBytes(StandardCharsets.US_ASCII)));
        assertThrows(InvalidDexException.class, () -> Model.read(new byte[0]));
        assertThrows(InvalidDexException.class, () -> Model.readFile(data("TestDex.java")));
        NoSuchFileException missing =
                assertThrows(
                        NoSuchFileException.class,
                        () -> Model.readFile(data("does_not_exist.dex")));
        assertEquals(data("does_not_exist.dex").toString(), missing.getFile());
        FileSystemException folder =
                assertThrows(FileSystemException.class, () -> Model.readFile(dir));
        assertTrue(folder.getMessage().contains("directory"), folder.getMessage());

        // The model does not read the DEX version 41 container format.
        byte[] dex = Files.readAllBytes(data("shapes.dex"));
        System.arraycopy("041".getBytes(StandardCharsets.US_ASCII), 0, dex, 4, 3);
        UnsupportedDexException e =
                assertThrows(UnsupportedDexException.class, () -> Model.read(dex));
        assertEquals(ErrorCode.UNSUPPORTED_DEX, e.code());
    }

    @Test
    void invalidModelExceptionListsProblems(@TempDir Path dir) throws IOException {
        try (Model m = read("shapes.dex")) {
            m.renameTypes(Map.of("LCircle;", "not a descriptor"));
            List<Problem> want = m.validate();
            assertFalse(want.isEmpty());
            for (Executable write :
                    List.<Executable>of(m::write, () -> m.writeFile(dir.resolve("x.dex")))) {
                InvalidModelException e = assertThrows(InvalidModelException.class, write);
                assertEquals(ErrorCode.INVALID_MODEL, e.code());
                assertEquals(want, e.problems());
                assertTrue(e.getMessage().contains("not a descriptor"), e.getMessage());
            }
        }
    }

    @Test
    void versionTooLow() throws IOException {
        // inherit.dex has default interface methods, which need DEX version 37.
        try (Model m = read("inherit.dex")) {
            assertEquals(37, m.requiredVersion());
            m.setVersion(35);
            VersionTooLowException e = assertThrows(VersionTooLowException.class, m::write);
            assertEquals(ErrorCode.VERSION_TOO_LOW, e.code());
            m.setVersion(m.requiredVersion());
            assertTrue(m.write().length > 0);
        }
    }

    @Test
    void poolOverflow() throws IOException {
        // A DEX file holds at most 65536 fields. The class in this fixture has 7
        // fields of its own, so 9400 copies of it, each renamed into its own
        // package, need more than that.
        try (Model source = Model.readFile(data("multistring.dex"), false);
                Model merged = new Model(source.version())) {
            for (int i = 0; i < 9400; i++) {
                try (Model part = source.copy()) {
                    Map<String, String> mapping = new HashMap<>();
                    for (String d : part.classDescriptors()) {
                        mapping.put(d, "Lcopy" + i + "/" + d.substring(1));
                    }
                    part.renameTypes(mapping);
                    for (String d : part.classDescriptors()) {
                        merged.moveClass(part, d);
                    }
                }
            }
            assertTrue(merged.poolStats().fields() > 65536);
            PoolOverflowException e = assertThrows(PoolOverflowException.class, merged::write);
            assertTrue(e.getMessage().contains("65536"), e.getMessage());
        }
    }

    @Test
    void cannotWrite(@TempDir Path dir) throws IOException {
        try (Model m = read("shapes.dex")) {
            for (Path path : List.of(dir.resolve("no/such/folder/out.dex"), dir)) {
                FileSystemException e =
                        assertThrows(FileSystemException.class, () -> m.writeFile(path));
                assertFalse(e instanceof NoSuchFileException, e.toString());
                assertEquals(path.toString(), e.getFile());
            }
            // A full disk: on Linux every write to /dev/full fails.
            Path full = Path.of("/dev/full");
            if (Files.exists(full)) {
                assertThrows(FileSystemException.class, () -> m.writeFile(full));
            }
        }
    }
}
