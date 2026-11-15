package io.github.burogurama.dexpp;

import static io.github.burogurama.dexpp.TestData.cls;
import static io.github.burogurama.dexpp.TestData.data;
import static io.github.burogurama.dexpp.TestData.dex;
import static io.github.burogurama.dexpp.TestData.field;
import static io.github.burogurama.dexpp.TestData.method;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.function.Executable;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.FileSystemException;
import java.nio.file.Files;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** Loading, classes, methods, fields, annotations, handles and errors. */
class AnalysisTest {
    private static List<String> classDescriptors(AnalysisContext ctx) {
        List<String> descriptors = new ArrayList<>();
        for (DexClass c : ctx.classes()) {
            descriptors.add(c.descriptor());
        }
        return descriptors;
    }

    // ---- loading --------------------------------------------------------------

    @Test
    void versionMatchesTheHeaderAndThePom() throws IOException {
        String header = Files.readString(TestData.ROOT.resolve("src/version.hpp"));
        Matcher m = Pattern.compile("#define DEXPP_VERSION \"([^\"]+)\"").matcher(header);
        assertTrue(m.find());
        assertEquals(m.group(1), Dexpp.VERSION);
        assertEquals(m.group(1), System.getProperty("dexpp.pom.version"), "the version in pom.xml");
    }

    @Test
    void loadAndFindClasses() {
        try (AnalysisContext ctx = dex("classes.dex")) {
            assertTrue(classDescriptors(ctx).contains("LTestDex;"));
            assertTrue(cls(ctx, "LTestDex;").isPublic());
            assertEquals(Optional.empty(), ctx.findClass("LNoSuch;"));
        }
    }

    @Test
    void fromApk() throws IOException {
        try (AnalysisContext ctx = AnalysisContext.fromApk(data("app.apk"))) {
            assertTrue(ctx.findClass("LTestDex;").isPresent());
            assertTrue(ctx.findClass("LFibonacci;").isPresent());
        }
    }

    @Test
    void fromApkWithAnyThreadCount() throws IOException {
        // multidex.apk has four deflated DEX entries, enough for the parallel path.
        List<String> serial;
        try (AnalysisContext ctx = AnalysisContext.fromApk(data("multidex.apk"), 1)) {
            serial = classDescriptors(ctx);
        }
        assertEquals("LTestDex;", serial.get(0));
        for (int threads : new int[] {4, 0, -3}) {
            try (AnalysisContext ctx = AnalysisContext.fromApk(data("multidex.apk"), threads)) {
                assertEquals(serial, classDescriptors(ctx), "threads " + threads);
            }
        }
        try (AnalysisContext ctx = AnalysisContext.fromApk(data("multidex.apk"))) {
            assertEquals(serial, classDescriptors(ctx));
        }
    }

    @Test
    void fromDexFiles() throws IOException {
        // The cross-DEX split of inherit.dex: the classes of both halves load.
        List<String> want = new ArrayList<>();
        try (AnalysisContext a = dex("inherit_a.dex");
                AnalysisContext b = dex("inherit_b.dex")) {
            want.addAll(classDescriptors(a));
            want.addAll(classDescriptors(b));
        }
        try (AnalysisContext ctx =
                AnalysisContext.fromDexFiles(
                        List.of(data("inherit_a.dex"), data("inherit_b.dex")))) {
            assertEquals(want, classDescriptors(ctx));
        }
        try (AnalysisContext empty = AnalysisContext.fromDexFiles(List.of())) {
            assertEquals(List.of(), empty.classes());
        }
    }

    @Test
    void strings() {
        try (AnalysisContext ctx = dex("classes.dex")) {
            List<String> strings = ctx.strings();
            assertTrue(strings.contains("Hello Dex++!!"));
            assertTrue(strings.contains("LTestDex;"));
            try (Xrefs xrefs = ctx.xrefs()) {
                assertTrue(strings.containsAll(xrefs.referencedStrings()));
            }
        }
        // A string with a NUL character keeps it.
        try (AnalysisContext multi = dex("multistring.dex")) {
            assertTrue(multi.strings().contains("test\0embedded"));
        }
    }

    // ---- errors ---------------------------------------------------------------

    @Test
    void fileErrorsAreIoExceptions(@TempDir Path dir) {
        Path missing = data("nonexistent.dex");
        NoSuchFileException notFound =
                assertThrows(NoSuchFileException.class, () -> AnalysisContext.fromDex(missing));
        assertEquals(missing.toString(), notFound.getFile());
        assertNull(notFound.getReason());

        FileSystemException folder =
                assertThrows(FileSystemException.class, () -> AnalysisContext.fromDex(dir));
        assertFalse(folder instanceof NoSuchFileException);
        assertEquals(dir.toString(), folder.getFile());
        assertEquals("it is a directory", folder.getReason());

        // For several files, the exception names the one that failed.
        NoSuchFileException second =
                assertThrows(
                        NoSuchFileException.class,
                        () -> AnalysisContext.fromDexFiles(List.of(data("classes.dex"), missing)));
        assertEquals(missing.toString(), second.getFile());

        for (Executable load :
                List.<Executable>of(
                        () -> AnalysisContext.fromApk(data("missing.apk")),
                        () -> Apk.open(data("missing.apk")),
                        () -> Model.readFile(data("missing.dex")))) {
            assertThrows(NoSuchFileException.class, load);
        }
        for (Executable load :
                List.<Executable>of(
                        () -> AnalysisContext.fromApk(dir),
                        () -> Apk.open(dir),
                        () -> Model.readFile(dir))) {
            FileSystemException e = assertThrows(FileSystemException.class, load);
            assertFalse(e instanceof NoSuchFileException);
        }
    }

    @Test
    void formatErrorsAreDexppExceptions() {
        Map<Executable, ErrorCode> cases =
                Map.of(
                        () -> AnalysisContext.fromDex(data("TestDex.java")),
                        ErrorCode.INVALID_DEX,
                        () ->
                                AnalysisContext.fromDexFiles(
                                        List.of(data("classes.dex"), data("TestDex.java"))),
                        ErrorCode.INVALID_DEX,
                        () -> AnalysisContext.fromApk(data("nodex.apk")),
                        ErrorCode.INVALID_APK,
                        () -> AnalysisContext.fromApk(data("classes.dex")),
                        ErrorCode.INVALID_APK,
                        () -> Apk.open(data("classes.dex")),
                        ErrorCode.INVALID_APK);
        for (Map.Entry<Executable, ErrorCode> c : cases.entrySet()) {
            DexppException e = assertThrows(DexppException.class, c.getKey());
            assertEquals(c.getValue(), e.code());
            assertFalse(e.getMessage().isEmpty());
            Class<? extends DexppException> type =
                    c.getValue() == ErrorCode.INVALID_DEX
                            ? InvalidDexException.class
                            : InvalidApkException.class;
            assertInstanceOf(type, e);
        }
    }

    @Test
    void errorCodesMatchTheCApi() {
        int value = 1;
        for (ErrorCode code : ErrorCode.values()) {
            assertEquals(value, code.value());
            assertEquals(code, ErrorCode.of(value));
            value++;
        }
        assertEquals(12, value); // DEXPP_ERROR_FILE_NOT_FOUND (1) to DEXPP_ERROR_INTERNAL (11)
        assertThrows(IllegalArgumentException.class, () -> ErrorCode.of(99));
        assertEquals(ErrorCode.MODEL, new ModelException("m").code());
        assertEquals(ErrorCode.POOL_OVERFLOW, new PoolOverflowException("m").code());
        assertEquals(ErrorCode.VERSION_TOO_LOW, new VersionTooLowException("m").code());
        assertEquals(ErrorCode.UNSUPPORTED_DEX, new UnsupportedDexException("m").code());
        InvalidModelException invalid =
                new InvalidModelException("m", List.of(new Problem("", "x")));
        assertEquals(ErrorCode.INVALID_MODEL, invalid.code());
        assertInstanceOf(ModelException.class, invalid);
        assertThrows(UnsupportedOperationException.class, () -> invalid.problems().clear());
    }

    // ---- classes, methods, fields ---------------------------------------------

    @Test
    void packageUsesDotsLikeTheManifest() {
        try (AnalysisContext ctx = dex("packaged.dex")) {
            assertEquals("com.example.app", cls(ctx, "Lcom/example/app/Packaged;").packageName());
            assertEquals(
                    "com.example.app", cls(ctx, "Lcom/example/app/Packaged$Inner;").packageName());
        }
    }

    @Test
    void classes() {
        try (AnalysisContext ctx = dex("shapes.dex")) {
            DexClass circle = cls(ctx, "LCircle;");
            assertEquals("LCircle;", circle.descriptor());
            assertEquals("", circle.packageName());
            ClassRef sup = circle.superclass().orElseThrow();
            assertEquals("LAbstractShape;", sup.descriptor());
            assertTrue(sup.isResolved());
            assertEquals("LAbstractShape;", sup.resolve().orElseThrow().descriptor());
            assertEquals(Optional.of("Shapes.java"), circle.sourceFile());

            DexClass abstractShape = cls(ctx, "LAbstractShape;");
            assertTrue(abstractShape.isAbstract());
            assertFalse(abstractShape.isInterface());
            List<ClassRef> interfaces = abstractShape.interfaces();
            assertEquals(1, interfaces.size());
            assertEquals("LShape;", interfaces.get(0).descriptor());

            DexClass shape = cls(ctx, "LShape;");
            assertTrue(shape.isInterface());
            assertTrue(shape.isAbstract());
            assertTrue(shape.accessFlags().has(AccessFlags.INTERFACE));

            // The superclass of a class outside the loaded files does not resolve.
            ClassRef object = shape.superclass().orElseThrow();
            assertEquals("Ljava/lang/Object;", object.descriptor());
            assertFalse(object.isResolved());
            assertEquals(Optional.empty(), object.resolve());
        }
    }

    @Test
    void methods() {
        try (AnalysisContext ctx = dex("classes.dex")) {
            DexClass cls = cls(ctx, "LTestDex;");
            List<String> names = new ArrayList<>();
            for (DexMethod m : cls.methods()) {
                names.add(m.name());
            }
            assertEquals(List.of("<init>", "helloDex", "main"), names);

            DexMethod hello = method(cls, "helloDex");
            assertTrue(hello.isStatic());
            assertTrue(hello.hasCode());
            assertTrue(hello.isPublic());
            assertFalse(hello.isPrivate());
            assertEquals("Ljava/lang/String;", hello.returnType());
            assertEquals(List.of(), hello.parameters());
            assertEquals("()Ljava/lang/String;", hello.proto());
            assertEquals("LTestDex;", hello.declaringClass().descriptor());
            MethodId id = new MethodId("LTestDex;", "helloDex", "()Ljava/lang/String;");
            assertEquals(id, hello.id());
            assertEquals("LTestDex;->helloDex()Ljava/lang/String;", id.toString());
            assertEquals(id.toString(), hello.toString());

            DexMethod main = method(cls, "main");
            assertEquals(List.of("[Ljava/lang/String;"), main.parameters());
            assertFalse(main.isConstructor());
            DexMethod init = method(cls, "<init>");
            assertTrue(init.isConstructor());
            assertTrue(init.accessFlags().has(AccessFlags.CONSTRUCTOR));
        }
        // An abstract method has no code.
        try (AnalysisContext pets = dex("polymorphic.dex")) {
            DexMethod sound = method(cls(pets, "LAnimal;"), "sound");
            assertFalse(sound.hasCode());
            assertTrue(sound.isAbstract());
            assertFalse(sound.isNative());
            assertEquals(0, sound.registerCount());
        }
    }

    @Test
    void accessFlags() {
        try (AnalysisContext ctx = dex("classes.dex")) {
            DexClass cls = cls(ctx, "LTestDex;");
            AccessFlags flags = cls.accessFlags();
            assertEquals(AccessFlags.PUBLIC, flags);
            assertTrue(flags.has(AccessFlags.PUBLIC));
            assertFalse(flags.has(AccessFlags.FINAL));
            AccessFlags hello = method(cls, "helloDex").accessFlags();
            assertEquals(AccessFlags.of(AccessFlags.PUBLIC, AccessFlags.STATIC), hello);
            assertEquals(AccessFlags.PUBLIC.with(AccessFlags.STATIC), hello);
            assertEquals("public|static", hello.toString());
        }
        // The DEX format gives two pairs of flags the same bit.
        assertEquals(AccessFlags.VOLATILE, AccessFlags.BRIDGE);
        assertEquals(AccessFlags.TRANSIENT, AccessFlags.VARARGS);
        assertEquals("0", AccessFlags.NONE.toString());
        assertEquals(
                "volatile/bridge|0x80000000",
                AccessFlags.VOLATILE.with(new AccessFlags(0x80000000)).toString());
    }

    @Test
    void fields() {
        try (AnalysisContext ctx = dex("annotated.dex")) {
            DexClass cls = cls(ctx, "LAnnotated;");
            List<String> names = new ArrayList<>();
            for (DexField f : cls.fields()) {
                names.add(f.name());
            }
            Collections.sort(names);
            assertEquals(List.of("BIG", "FLAG", "GREETING", "MAGIC", "PI", "counter"), names);
            DexField magic = field(cls, "MAGIC");
            assertEquals("I", magic.type());
            assertTrue(magic.isStatic());
            assertTrue(magic.isFinal());
            assertFalse(magic.isPublic());
            assertEquals("LAnnotated;", magic.declaringClass().descriptor());
            FieldId id = new FieldId("LAnnotated;", "MAGIC", "I");
            assertEquals(id, magic.id());
            assertEquals("LAnnotated;->MAGIC:I", id.toString());
        }
    }

    @Test
    void initialValues() {
        try (AnalysisContext ctx = dex("annotated.dex")) {
            DexClass cls = cls(ctx, "LAnnotated;");
            Map<String, AnnotationValue> want =
                    Map.of(
                            "MAGIC", new AnnotationValue.IntValue(42),
                            "GREETING", new AnnotationValue.StringValue("hello"),
                            "BIG", new AnnotationValue.IntValue(1234567890123L),
                            "PI", new AnnotationValue.FloatValue(3.25),
                            "FLAG", new AnnotationValue.BooleanValue(true));
            for (Map.Entry<String, AnnotationValue> e : want.entrySet()) {
                assertEquals(
                        Optional.of(e.getValue()),
                        field(cls, e.getKey()).initialValue(),
                        e.getKey());
            }
            assertEquals(Optional.empty(), field(cls, "counter").initialValue());
        }
    }

    private static Annotation find(List<Annotation> list, String descriptor) {
        for (Annotation a : list) {
            if (a.typeDescriptor().equals(descriptor)) {
                return a;
            }
        }
        throw new AssertionError("no " + descriptor + " annotation in " + list);
    }

    private static AnnotationValue element(Annotation a, String name) {
        return a.element(name)
                .orElseThrow(
                        () -> new AssertionError(a.typeDescriptor() + " has no element " + name));
    }

    @Test
    void annotations() {
        try (AnalysisContext ctx = dex("annotated.dex")) {
            DexClass cls = cls(ctx, "LAnnotated;");
            Annotation marker = find(cls.annotations(), "LMarker;");
            assertEquals(AnnotationVisibility.RUNTIME, marker.visibility());
            assertEquals(new AnnotationValue.StringValue("cls"), element(marker, "name"));
            assertEquals(new AnnotationValue.IntValue(3), element(marker, "level"));
            assertEquals(
                    new AnnotationValue.ArrayValue(
                            List.of(
                                    new AnnotationValue.StringValue("a"),
                                    new AnnotationValue.StringValue("b"))),
                    element(marker, "tags"));
            assertEquals(Optional.empty(), marker.element("target"), "left at its default");

            Annotation greet = find(method(cls, "greet").annotations(), "LMarker;");
            assertEquals(
                    new AnnotationValue.TypeValue("Ljava/lang/String;"), element(greet, "target"));
            find(method(cls, "old").annotations(), "Ljava/lang/Deprecated;");
            assertEquals(
                    new AnnotationValue.StringValue("fld"),
                    element(find(field(cls, "MAGIC").annotations(), "LMarker;"), "name"));

            // The annotation class itself: an enum value, an array of enum values,
            // and the defaults as a nested annotation with an empty array.
            DexClass m = cls(ctx, "LMarker;");
            Annotation retention = find(m.annotations(), "Ljava/lang/annotation/Retention;");
            assertEquals(
                    new AnnotationValue.EnumValue(
                            new FieldId(
                                    "Ljava/lang/annotation/RetentionPolicy;",
                                    "RUNTIME",
                                    "Ljava/lang/annotation/RetentionPolicy;")),
                    element(retention, "value"));
            Annotation target = find(m.annotations(), "Ljava/lang/annotation/Target;");
            AnnotationValue.ArrayValue kinds =
                    assertInstanceOf(AnnotationValue.ArrayValue.class, element(target, "value"));
            assertEquals(3, kinds.elements().size());
            AnnotationValue.EnumValue second =
                    assertInstanceOf(AnnotationValue.EnumValue.class, kinds.elements().get(1));
            assertEquals("METHOD", second.constant().name());
            Annotation defaults = find(m.annotations(), "Ldalvik/annotation/AnnotationDefault;");
            assertEquals(AnnotationVisibility.SYSTEM, defaults.visibility());
            Annotation nested = assertInstanceOf(Annotation.class, element(defaults, "value"));
            assertEquals("LMarker;", nested.typeDescriptor());
            assertEquals(new AnnotationValue.IntValue(1), element(nested, "level"));
            assertEquals(new AnnotationValue.ArrayValue(List.of()), element(nested, "tags"));
            assertEquals(
                    new AnnotationValue.TypeValue("Ljava/lang/Object;"), element(nested, "target"));
        }
    }

    @Test
    void methodHandleAndMethodTypeValues() {
        // handles.dex: @HandleMarker(getter = <static-get Handles.counter>,
        // invoker = <invoke-static Handles.target(I)I>, type = <method type (I)I>).
        try (AnalysisContext ctx = dex("handles.dex")) {
            Annotation marker = find(cls(ctx, "LHandles;").annotations(), "LHandleMarker;");
            assertEquals(
                    new AnnotationValue.MethodHandleValue(
                            MethodHandleKind.STATIC_GET, new FieldId("LHandles;", "counter", "I")),
                    element(marker, "getter"));
            AnnotationValue.MethodHandleValue invoker =
                    assertInstanceOf(
                            AnnotationValue.MethodHandleValue.class, element(marker, "invoker"));
            assertEquals(MethodHandleKind.INVOKE_STATIC, invoker.kind());
            String target =
                    switch (invoker.member()) {
                        case MethodId method -> method.name() + method.proto();
                        case FieldId field -> field.name();
                    };
            assertEquals("target(I)I", target);
            assertEquals(new AnnotationValue.MethodTypeValue("(I)I"), element(marker, "type"));
        }
    }

    /** Every value of every annotation of every fixture converts. */
    @Test
    void everyAnnotationOfEveryFixtureConverts() {
        int count = 0;
        for (String name :
                List.of(
                        "annotated.dex",
                        "inherit.dex",
                        "polymorphic.dex",
                        "shapes.dex",
                        "classes.dex",
                        "fibonacci.dex",
                        "trycatch.dex",
                        "multistring.dex",
                        "handles.dex")) {
            try (AnalysisContext ctx = dex(name)) {
                for (DexClass c : ctx.classes()) {
                    List<AnnotationValue> values = new ArrayList<>(c.annotations());
                    for (DexMethod m : c.methods()) {
                        values.addAll(m.annotations());
                    }
                    for (DexField f : c.fields()) {
                        values.addAll(f.annotations());
                        f.initialValue().ifPresent(values::add);
                    }
                    for (AnnotationValue v : values) {
                        count += walk(v);
                    }
                }
            }
        }
        assertTrue(count >= 40, "only " + count + " values");
    }

    private static int walk(AnnotationValue value) {
        return switch (value) {
            case AnnotationValue.ArrayValue(List<AnnotationValue> elements) ->
                    1 + elements.stream().mapToInt(AnalysisTest::walk).sum();
            case Annotation a -> 1 + a.elements().stream().mapToInt(e -> walk(e.value())).sum();
            case AnnotationValue.NullValue _ -> 1;
            case AnnotationValue.BooleanValue _ -> 1;
            case AnnotationValue.IntValue _ -> 1;
            case AnnotationValue.FloatValue _ -> 1;
            case AnnotationValue.StringValue _ -> 1;
            case AnnotationValue.TypeValue _ -> 1;
            case AnnotationValue.EnumValue _ -> 1;
            case AnnotationValue.FieldValue _ -> 1;
            case AnnotationValue.MethodValue _ -> 1;
            case AnnotationValue.MethodTypeValue _ -> 1;
            case AnnotationValue.MethodHandleValue _ -> 1;
        };
    }

    // ---- handles --------------------------------------------------------------

    @Test
    void handlesOutliveTheirContext() {
        AnalysisContext ctx = dex("classes.dex");
        DexClass cls = cls(ctx, "LTestDex;");
        List<DexMethod> methods = cls.methods();
        ctx.close();
        ctx.close(); // a second close does nothing
        System.gc();
        assertEquals("LTestDex;", cls.descriptor());
        cls.close();
        for (DexMethod m : methods) {
            assertFalse(m.name().isEmpty());
            assertFalse(m.instructions().isEmpty());
        }
    }

    /**
     * A handle that nothing else refers to survives a garbage collection in the middle of a chain
     * of calls (the reachability fence in NativeHandle).
     */
    @Test
    void chainedCallsSurviveGc() {
        for (int i = 0; i < 50; i++) {
            String descriptor =
                    dex("fibonacci.dex")
                            .findClass("LFibonacci;")
                            .orElseThrow()
                            .methods()
                            .get(0)
                            .declaringClass()
                            .resolve()
                            .orElseThrow()
                            .descriptor();
            assertEquals("LFibonacci;", descriptor);
            System.gc();
        }
    }

    @Test
    void closedHandlesThrow() throws IOException {
        try (AnalysisContext ctx = dex("polymorphic.dex")) {
            DexClass cls = cls(ctx, "LDog;");
            DexMethod method = cls.methods().get(0);
            DexField field = cls(ctx, "LPet;").fields().get(0);
            ClassRef ref = cls.superclass().orElseThrow();
            CallGraph graph = ctx.callGraph();
            ClassHierarchy hierarchy = ctx.classHierarchy();
            Xrefs xrefs = ctx.xrefs();
            Apk apk = Apk.open(data("sample.apk"));
            ResourceTable resources = apk.resources().orElseThrow();
            Model model = new Model(35);
            for (AutoCloseable h :
                    List.<AutoCloseable>of(
                            cls, method, field, ref, graph, hierarchy, xrefs, apk, model)) {
                try {
                    h.close();
                    h.close();
                } catch (Exception e) {
                    throw new AssertionError(e);
                }
            }
            Map<String, Executable> uses =
                    Map.of(
                            "DexClass", cls::descriptor,
                            "DexMethod", method::instructions,
                            "DexField", field::type,
                            "ClassRef", ref::descriptor,
                            "CallGraph", graph::nodes,
                            "ClassHierarchy", hierarchy::isEmpty,
                            "Xrefs", xrefs::referrers,
                            "Apk", apk::entries,
                            "Model", model::write);
            for (Map.Entry<String, Executable> use : uses.entrySet()) {
                IllegalStateException e = assertThrows(IllegalStateException.class, use.getValue());
                assertEquals("dexpp: " + use.getKey() + " used after close()", e.getMessage());
            }
            IllegalStateException e = assertThrows(IllegalStateException.class, resources::isEmpty);
            assertEquals("dexpp: Apk used after close()", e.getMessage());
            assertEquals("DexClass (closed)", cls.toString());

            Model open = new Model(35);
            assertThrows(IllegalStateException.class, () -> open.copyClass(model, "LA;"));
            open.close();
        }
    }

    /** Reading from one analysis on several threads at once is safe. */
    @Test
    void concurrentReads() throws Exception {
        try (AnalysisContext ctx = dex("polymorphic.dex")) {
            List<DexClass> classes = ctx.classes();
            ExecutorService pool = Executors.newFixedThreadPool(8);
            try {
                List<Future<Integer>> results = new ArrayList<>();
                for (int t = 0; t < 8; t++) {
                    results.add(
                            pool.submit(() -> {
                                int n = 0;
                                for (DexClass c : classes) {
                                    for (DexMethod m : c.methods()) {
                                        for (Instruction insn : m.instructions()) {
                                            n += Dexpp.toString(insn).length();
                                        }
                                        n += m.cfg().size() + m.calls().size();
                                    }
                                }
                                try (CallGraph g = ctx.callGraph();
                                        Xrefs x = ctx.xrefs()) {
                                    n += g.edgeCount() + x.referrers().size();
                                }
                                return n;
                            }));
                }
                int first = results.get(0).get();
                for (Future<Integer> r : results) {
                    assertEquals(first, r.get());
                }
            } finally {
                pool.shutdown();
            }
        }
    }
}
