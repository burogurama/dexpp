package io.github.burogurama.dexpp;

import static org.junit.jupiter.api.Assertions.assertEquals;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.OptionalInt;

/**
 * The examples that docs/java.md shows. The page includes the code between the "--8<--
 * [start:name]" and "--8<-- [end:name]" markers, and the output from
 * src/test/resources/examples/NAME.txt. Each test runs one example and compares what it prints with
 * that file, so the code and the output on the page are checked.
 *
 * <p>After changing an example, write the output files again with {@code mvn test
 * -Dtest=ExamplesTest -Ddexpp.examples.update=true} and look at the diff.
 *
 * <p>The tests run in bindings/java, so the examples open the test files of the repository by the
 * relative path ../../tests/data.
 */
class ExamplesTest {
    @FunctionalInterface
    private interface Example {
        void run() throws IOException;
    }

    /** Runs an example, and checks what it prints against its output file. */
    private static void check(String name, Example example) throws IOException {
        PrintStream saved = System.out;
        ByteArrayOutputStream buffer = new ByteArrayOutputStream();
        System.setOut(new PrintStream(buffer, true, StandardCharsets.UTF_8));
        try {
            example.run();
        } finally {
            System.setOut(saved);
        }
        String printed = buffer.toString(StandardCharsets.UTF_8).replace("\r\n", "\n");
        if (Boolean.getBoolean("dexpp.examples.update")) {
            Path file = Path.of("src", "test", "resources", "examples", name + ".txt");
            Files.createDirectories(file.getParent());
            Files.writeString(file, printed, StandardCharsets.UTF_8);
            return;
        }
        try (InputStream in =
                ExamplesTest.class.getResourceAsStream("/examples/" + name + ".txt")) {
            if (in == null) {
                throw new AssertionError("no output file for the example " + name);
            }
            String expected =
                    new String(in.readAllBytes(), StandardCharsets.UTF_8).replace("\r\n", "\n");
            assertEquals(expected, printed, "output of the example " + name);
        }
    }

    @Test
    void load() throws IOException {
        check("load", ExamplesTest::loadExample);
    }

    @Test
    void instructions() throws IOException {
        check("instructions", ExamplesTest::instructionsExample);
    }

    @Test
    void cfg() throws IOException {
        check("cfg", ExamplesTest::cfgExample);
    }

    @Test
    void references() throws IOException {
        check("references", ExamplesTest::referencesExample);
    }

    @Test
    void annotations() throws IOException {
        check("annotations", ExamplesTest::annotationsExample);
    }

    @Test
    void callGraph() throws IOException {
        check("callgraph", ExamplesTest::callGraphExample);
    }

    @Test
    void hierarchy() throws IOException {
        check("hierarchy", ExamplesTest::hierarchyExample);
    }

    @Test
    void apk() throws IOException {
        check("apk", ExamplesTest::apkExample);
    }

    @Test
    void signing() throws IOException {
        check("signing", ExamplesTest::signingExample);
    }

    @Test
    void model(@TempDir Path dir) throws IOException {
        check("model", () -> modelExample(dir));
    }

    @Test
    void errors() throws IOException {
        check("errors", ExamplesTest::errorsExample);
    }

    // ---- the examples -------------------------------------------------------------------------

    static void loadExample() throws IOException {
        // --8<-- [start:load]
        Path path = Path.of("../../tests/data/classes.dex");
        try (AnalysisContext ctx = AnalysisContext.fromDex(path)) {
            for (DexClass cls : ctx.classes()) {
                System.out.println(cls.descriptor());
                for (DexMethod m : cls.methods()) {
                    System.out.println("  " + m.name() + m.proto() + "  " + m.accessFlags());
                }
            }
            Optional<DexClass> missing = ctx.findClass("Lcom/example/Missing;");
            if (missing.isEmpty()) {
                System.out.println("no such class");
            }
        }
        // --8<-- [end:load]
    }

    static void instructionsExample() throws IOException {
        try (AnalysisContext ctx =
                AnalysisContext.fromDex(Path.of("../../tests/data/classes.dex"))) {
            // --8<-- [start:instructions]
            DexClass cls = ctx.findClass("LTestDex;").orElseThrow();
            for (DexMethod m : cls.methods()) {
                if (!m.name().equals("main")) {
                    continue;
                }
                for (Instruction insn : m.instructions()) {
                    System.out.printf("%04x  %s%n", insn.offset(), Dexpp.toString(insn));
                    switch (insn) {
                        case InvokeInstruction invoke ->
                                System.out.println("      call, arguments in " + invoke.argRegs());
                        case FieldInstruction field ->
                                System.out.println("      field read, static: " + field.isStatic());
                        default -> {}
                    }
                }
            }
            // --8<-- [end:instructions]
        }
    }

    static void cfgExample() throws IOException {
        try (AnalysisContext ctx =
                AnalysisContext.fromDex(Path.of("../../tests/data/trycatch.dex"))) {
            // --8<-- [start:cfg]
            DexClass cls = ctx.findClass("LTryCatch;").orElseThrow();
            for (DexMethod m : cls.methods()) {
                if (!m.name().equals("divide")) {
                    continue;
                }
                for (BasicBlock b : m.cfg().blocks()) {
                    System.out.printf(
                            "block %d [%d, %d)", b.index(), b.startOffset(), b.endOffset());
                    for (CfgEdge e : b.successors()) {
                        System.out.printf(" -> %d (%s", e.targetBlock(), e.kind());
                        e.catchType().ifPresent(type -> System.out.print(" " + type));
                        System.out.print(")");
                    }
                    System.out.println();
                }
                for (TryBlock t : m.tryBlocks()) {
                    for (CatchHandler h : t.handlers()) {
                        System.out.printf(
                                "try [%d, %d) catches %s at %d%n",
                                t.startOffset(),
                                t.endOffset(),
                                h.typeDescriptor(),
                                h.handlerOffset());
                    }
                }
            }
            // --8<-- [end:cfg]
        }
    }

    static void referencesExample() throws IOException {
        try (AnalysisContext ctx =
                AnalysisContext.fromDex(Path.of("../../tests/data/classes.dex"))) {
            // --8<-- [start:references]
            DexClass cls = ctx.findClass("LTestDex;").orElseThrow();
            for (DexMethod m : cls.methods()) {
                for (MethodCall c : m.calls()) {
                    System.out.println(m.name() + " calls " + c.target() + " with " + c.kind());
                }
                for (FieldAccess f : m.fieldAccesses()) {
                    System.out.println(m.name() + " reads " + f.field());
                }
                for (StringLoad s : m.stringLoads()) {
                    System.out.println(m.name() + " loads \"" + s.value() + "\"");
                }
            }
            // --8<-- [end:references]
        }
    }

    static void annotationsExample() throws IOException {
        try (AnalysisContext ctx =
                AnalysisContext.fromDex(Path.of("../../tests/data/annotated.dex"))) {
            // --8<-- [start:annotations]
            DexClass cls = ctx.findClass("LAnnotated;").orElseThrow();
            for (Annotation a : cls.annotations()) {
                System.out.println(a.typeDescriptor() + " " + a.visibility());
                for (AnnotationElement e : a.elements()) {
                    String text =
                            switch (e.value()) {
                                case AnnotationValue.IntValue(long number) ->
                                        "the number " + number;
                                case AnnotationValue.StringValue(String string) ->
                                        "the string \"" + string + "\"";
                                case AnnotationValue.ArrayValue(List<AnnotationValue> items) ->
                                        "an array of " + items.size() + " values";
                                default -> e.value().toString();
                            };
                    System.out.println("  " + e.name() + " = " + text);
                }
            }
            for (DexField f : cls.fields()) {
                Optional<AnnotationValue> value = f.initialValue();
                if (value.isPresent()) {
                    System.out.println(f.name() + " starts at " + value.get());
                }
            }
            // --8<-- [end:annotations]
        }
    }

    static void callGraphExample() throws IOException {
        Path path = Path.of("../../tests/data/polymorphic.dex");
        try (AnalysisContext ctx = AnalysisContext.fromDex(path)) {
            // --8<-- [start:callgraph]
            try (CallGraph g = ctx.callGraph()) {
                MethodId soundId = new MethodId("LAnimal;", "sound", "()Ljava/lang/String;");
                CallNode sound = g.find(soundId).orElseThrow();
                // A call to Animal.sound may run these overrides:
                for (int i : g.dispatchTargets(sound.index())) {
                    CallNode target = g.node(i).orElseThrow();
                    System.out.println("dispatches to " + target.id());
                }
                // The call sites that name Animal.sound:
                for (int i : g.inEdges(sound.index())) {
                    CallEdge e = g.edge(i).orElseThrow();
                    MethodId caller = g.node(e.callerNode()).orElseThrow().id();
                    System.out.printf(
                            "called by %s.%s %s %s%n",
                            caller.classDescriptor(), caller.name(), e.kind(), e.origin());
                }
            }
            // --8<-- [end:callgraph]
        }
    }

    static void hierarchyExample() throws IOException {
        Path path = Path.of("../../tests/data/polymorphic.dex");
        try (AnalysisContext ctx = AnalysisContext.fromDex(path)) {
            // --8<-- [start:hierarchy]
            try (ClassHierarchy h = ctx.classHierarchy();
                    Xrefs x = ctx.xrefs()) {
                System.out.println(h.allSupertypes("LDog;"));
                System.out.println(h.isSubtypeOf("LDog;", "LAnimal;"));

                // The groups come in no fixed order, so sort them.
                List<MethodRefGroup> groups = new ArrayList<>(x.methodRefsByName("sound"));
                groups.sort(Comparator.comparing((MethodRefGroup g) -> g.target().toString()));
                for (MethodRefGroup group : groups) {
                    for (MethodXref site : group.sites()) {
                        MethodId caller = x.referrerId(site.referrer()).orElseThrow();
                        System.out.printf(
                                "%s calls %s at %d%n",
                                caller.name(), group.target().classDescriptor(), site.codeOffset());
                    }
                }
            }
            // --8<-- [end:hierarchy]
        }
    }

    static void apkExample() throws IOException {
        // --8<-- [start:apk]
        try (Apk apk = Apk.open(Path.of("../../tests/data/sample.apk"))) {
            Optional<Manifest> manifest = apk.manifest();
            if (manifest.isPresent()) {
                Manifest m = manifest.get();
                System.out.println("package: " + m.packageName());
                m.minSdk().ifPresent((long sdk) -> System.out.println("min SDK: " + sdk));
                for (Component c : m.components()) {
                    System.out.printf(
                            "%-8s %s exported=%b reachable=%b%n",
                            c.kind(), c.name(), c.isExported(), c.isReachable());
                }
            }
            Optional<ResourceTable> resources = apk.resources();
            if (resources.isPresent()) {
                OptionalInt id = resources.get().idOf("string", "app_name");
                if (id.isPresent()) {
                    Optional<String> name = resources.get().resolveString(id.getAsInt());
                    System.out.println("@string/app_name = " + name.orElse("?"));
                }
            }
            System.out.println("signed: " + apk.signing().isSigned());

            // The code: every classes*.dex of the APK.
            try (AnalysisContext ctx = apk.analysis()) {
                System.out.println("classes: " + ctx.classes().size());
            }
        }
        // --8<-- [end:apk]
    }

    static void signingExample() throws IOException {
        try (Apk apk = Apk.open(Path.of("../../tests/data/signed.apk"))) {
            // --8<-- [start:signing]
            SigningInfo s = apk.signing();
            System.out.println("schemes: v1 " + s.v1() + ", v2 " + s.v2() + ", v3 " + s.v3());
            for (Certificate c : s.certificates()) {
                System.out.println(c.subject());
                System.out.println("sha256: " + c.sha256Hex());
            }
            // --8<-- [end:signing]
        }
    }

    static void modelExample(Path dir) throws IOException {
        // --8<-- [start:model]
        try (Model m = Model.readFile(Path.of("../../tests/data/shapes.dex"))) {
            List<String> descriptors = m.classDescriptors();
            for (int i = 0; i < descriptors.size(); i++) {
                try (Model one = new Model(m.version())) {
                    one.copyClass(m, descriptors.get(i));
                    one.writeFile(dir.resolve("class" + i + ".dex"));
                }
                System.out.println("wrote " + descriptors.get(i));
            }

            // Rename a class everywhere it is referenced, then check and write.
            m.renameTypes(Map.of("LCircle;", "Lcom/example/Round;"));
            List<Problem> problems = m.validate();
            if (problems.isEmpty()) {
                byte[] out = m.write();
                System.out.println("renamed: " + m.hasClass("Lcom/example/Round;"));
                System.out.println("written: " + (out.length > 0));
            }
        }
        // --8<-- [end:model]
    }

    static void errorsExample() throws IOException {
        // --8<-- [start:errors]
        try (AnalysisContext ctx = AnalysisContext.fromDex(Path.of("missing.dex"))) {
            System.out.println(ctx.classes().size());
        } catch (NoSuchFileException e) {
            System.out.println("no such file: " + e.getFile());
        }

        Path notDex = Path.of("../../tests/data/TestDex.java");
        try (AnalysisContext ctx = AnalysisContext.fromDex(notDex)) {
            System.out.println(ctx.classes().size());
        } catch (InvalidDexException e) {
            System.out.println("not a DEX file: " + e.code());
        }

        try (Model m = Model.readFile(Path.of("../../tests/data/shapes.dex"))) {
            m.renameTypes(Map.of("LCircle;", "not a descriptor"));
            byte[] out = m.write();
            System.out.println(out.length);
        } catch (InvalidModelException e) {
            System.out.println(e.code() + " with " + e.problems().size() + " problems");
            Problem first = e.problems().get(0);
            System.out.println(first.where() + ": " + first.message());
        }
        // --8<-- [end:errors]
    }
}
