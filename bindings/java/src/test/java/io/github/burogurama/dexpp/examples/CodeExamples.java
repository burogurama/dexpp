package io.github.burogurama.dexpp.examples;

import io.github.burogurama.dexpp.AccessFlags;
import io.github.burogurama.dexpp.AnalysisContext;
import io.github.burogurama.dexpp.Annotation;
import io.github.burogurama.dexpp.AnnotationElement;
import io.github.burogurama.dexpp.AnnotationValue;
import io.github.burogurama.dexpp.BasicBlock;
import io.github.burogurama.dexpp.CatchHandler;
import io.github.burogurama.dexpp.CfgEdge;
import io.github.burogurama.dexpp.ClassRef;
import io.github.burogurama.dexpp.DexClass;
import io.github.burogurama.dexpp.DexField;
import io.github.burogurama.dexpp.DexMethod;
import io.github.burogurama.dexpp.Dexpp;
import io.github.burogurama.dexpp.FieldAccess;
import io.github.burogurama.dexpp.FieldInstruction;
import io.github.burogurama.dexpp.Instruction;
import io.github.burogurama.dexpp.MethodCall;
import io.github.burogurama.dexpp.ParameterRegister;
import io.github.burogurama.dexpp.TryBlock;

import java.io.IOException;
import java.nio.file.Path;
import java.util.List;
import java.util.Optional;

/**
 * The examples of the guide "Classes, methods and bytecode". ExamplesTest runs them. Code outside
 * the markers is not shown: it sets up the variables that the earlier sections of the guide made.
 */
final class CodeExamples {
    private CodeExamples() {}

    private static final Path SHAPES = Path.of("../../tests/data/shapes.dex");

    /** Finds a method of a class by its name. */
    private static DexMethod methodNamed(DexClass cls, String name) {
        return cls.methods().stream()
                .filter((DexMethod method) -> method.name().equals(name))
                .findFirst()
                .orElseThrow();
    }

    static void load() throws IOException {
        // --8<-- [start:load]
        try (AnalysisContext apkCtx = AnalysisContext.fromApk(Path.of("../../tests/data/app.apk"));
                AnalysisContext dexCtx =
                        AnalysisContext.fromDex(Path.of("../../tests/data/classes.dex"));
                AnalysisContext splitCtx =
                        AnalysisContext.fromDexFiles(
                                List.of(
                                        Path.of("../../tests/data/inherit_a.dex"),
                                        Path.of("../../tests/data/inherit_b.dex")))) {
            System.out.println(apkCtx.classes().size() + " " + dexCtx.classes().size() + " "
                    + splitCtx.classes().size());
        }
        // --8<-- [end:load]
    }

    static void classes() throws IOException {
        // --8<-- [start:classes]
        Path path = Path.of("../../tests/data/shapes.dex");
        try (AnalysisContext ctx = AnalysisContext.fromDex(path)) {
            for (DexClass cls : ctx.classes()) {
                String kind = cls.isInterface() ? "interface" : "class";
                String parent = cls.superclass().map(ClassRef::descriptor).orElse("none");
                List<String> interfaces =
                        cls.interfaces().stream().map(ClassRef::descriptor).toList();
                System.out.println(kind + " " + cls.descriptor() + " extends " + parent
                        + " implements " + interfaces);
            }

            DexClass circle = ctx.findClass("LCircle;").orElseThrow();
            List<String> fields =
                    circle.fields().stream()
                            .map((DexField field) -> field.name() + ":" + field.type())
                            .toList();
            System.out.println(fields);

            Optional<DexClass> missing = ctx.findClass("Lno/such/Class;");
            System.out.println(missing);
        }
        // --8<-- [end:classes]
    }

    static void methods() throws IOException {
        try (AnalysisContext ctx = AnalysisContext.fromDex(SHAPES)) {
            DexClass circle = ctx.findClass("LCircle;").orElseThrow();
            // --8<-- [start:methods]
            for (DexMethod method : circle.methods()) {
                String visibility = method.isPublic() ? "public" : "non-public";
                System.out.println(visibility + " " + method.name() + " " + method.parameters()
                        + " -> " + method.returnType());
            }

            DexMethod constructor = circle.methods().get(0);
            AccessFlags flags = constructor.accessFlags();
            System.out.println(constructor.name() + " " + flags.value() + " "
                    + flags.has(AccessFlags.CONSTRUCTOR));
            // --8<-- [end:methods]
        }
    }

    static void instructions() throws IOException {
        try (AnalysisContext ctx = AnalysisContext.fromDex(SHAPES)) {
            DexClass circle = ctx.findClass("LCircle;").orElseThrow();
            // --8<-- [start:instructions]
            DexMethod area =
                    circle.methods().stream()
                            .filter((DexMethod method) -> method.name().equals("area"))
                            .findFirst()
                            .orElseThrow();
            List<Instruction> instructions = area.instructions();

            for (Instruction instruction : instructions) {
                System.out.printf("%04x  %s%n", instruction.offset(), Dexpp.toString(instruction));
            }
            // --8<-- [end:instructions]
        }
    }

    static void typed() throws IOException {
        try (AnalysisContext ctx = AnalysisContext.fromDex(SHAPES)) {
            DexMethod area = methodNamed(ctx.findClass("LCircle;").orElseThrow(), "area");
            List<Instruction> instructions = area.instructions();
            // --8<-- [start:typed]
            for (Instruction instruction : instructions) {
                if (instruction instanceof FieldInstruction field) {
                    String action = field.isWrite() ? "write" : "read";
                    System.out.println("field access at " + field.offset() + " " + action);
                }
            }

            List<String> called =
                    area.calls().stream()
                            .map((MethodCall call) ->
                                    call.target().classDescriptor() + " " + call.target().name())
                            .toList();
            List<String> accessed =
                    area.fieldAccesses().stream()
                            .map((FieldAccess access) ->
                                    access.field().name() + " write=" + access.isWrite())
                            .toList();
            System.out.println(called);
            System.out.println(accessed);
            // --8<-- [end:typed]
        }
    }

    static void registers() throws IOException {
        try (AnalysisContext ctx = AnalysisContext.fromDex(SHAPES)) {
            DexMethod area = methodNamed(ctx.findClass("LCircle;").orElseThrow(), "area");
            // --8<-- [start:registers]
            System.out.println(area.registerCount() + " registers");

            for (ParameterRegister parameter : area.parameterRegisters()) {
                String note = parameter.isThis() ? " (this)" : "";
                System.out.println("v" + parameter.reg() + " " + parameter.type() + note);
            }
            // --8<-- [end:registers]
        }
    }

    static void cfg() throws IOException {
        // --8<-- [start:cfg]
        Path fibonacciPath = Path.of("../../tests/data/fibonacci.dex");
        try (AnalysisContext fibonacciCtx = AnalysisContext.fromDex(fibonacciPath)) {
            DexClass fibonacci = fibonacciCtx.findClass("LFibonacci;").orElseThrow();
            DexMethod compute =
                    fibonacci.methods().stream()
                            .filter((DexMethod method) -> method.name().equals("compute"))
                            .findFirst()
                            .orElseThrow();

            for (BasicBlock block : compute.cfg().blocks()) {
                List<String> edges =
                        block.successors().stream()
                                .map((CfgEdge edge) -> edge.kind() + "->" + edge.targetBlock())
                                .toList();
                System.out.printf("block %d [%#x, %#x) %s%n",
                        block.index(), block.startOffset(), block.endOffset(), edges);
            }
        }
        // --8<-- [end:cfg]
    }

    static void handlers() throws IOException {
        // --8<-- [start:handlers]
        Path tryCatchPath = Path.of("../../tests/data/trycatch.dex");
        try (AnalysisContext parseCtx = AnalysisContext.fromDex(tryCatchPath)) {
            DexClass parseClass = parseCtx.findClass("LTryCatch;").orElseThrow();
            DexMethod safeParse =
                    parseClass.methods().stream()
                            .filter((DexMethod method) -> method.name().equals("safeParse"))
                            .findFirst()
                            .orElseThrow();

            for (BasicBlock block : safeParse.cfg().blocks()) {
                for (CfgEdge edge : block.successors()) {
                    switch (edge.kind()) {
                        case EXCEPTION -> System.out.printf("block %d -> %d: catches %s%n",
                                block.index(), edge.targetBlock(), edge.catchType().orElseThrow());
                        case EXCEPTION_CATCH_ALL -> System.out.printf(
                                "block %d -> %d: catches everything%n",
                                block.index(), edge.targetBlock());
                        default -> {}
                    }
                }
            }
        }
        // --8<-- [end:handlers]
    }

    static void tryBlocks() throws IOException {
        // --8<-- [start:try]
        Path tryCatchPath = Path.of("../../tests/data/trycatch.dex");
        try (AnalysisContext tryCatchCtx = AnalysisContext.fromDex(tryCatchPath)) {
            DexClass tryCatch = tryCatchCtx.findClass("LTryCatch;").orElseThrow();
            DexMethod divide =
                    tryCatch.methods().stream()
                            .filter((DexMethod method) -> method.name().equals("divide"))
                            .findFirst()
                            .orElseThrow();

            for (TryBlock tryBlock : divide.tryBlocks()) {
                System.out.printf("try [%#x, %#x)%n", tryBlock.startOffset(), tryBlock.endOffset());
                for (CatchHandler handler : tryBlock.handlers()) {
                    System.out.printf("  catch %s at %#x%n",
                            handler.typeDescriptor(), handler.handlerOffset());
                }
                System.out.println("  catch-all at " + tryBlock.catchAllOffset());
            }
        }
        // --8<-- [end:try]
    }

    static void annotations() throws IOException {
        // --8<-- [start:annotations]
        Path annotatedPath = Path.of("../../tests/data/annotated.dex");
        try (AnalysisContext annotatedCtx = AnalysisContext.fromDex(annotatedPath)) {
            DexClass annotated = annotatedCtx.findClass("LAnnotated;").orElseThrow();

            for (Annotation annotation : annotated.annotations()) {
                System.out.println(annotation.typeDescriptor() + " " + annotation.visibility());
                for (AnnotationElement element : annotation.elements()) {
                    String text =
                            switch (element.value()) {
                                case AnnotationValue.IntValue(long number) ->
                                        "the number " + number;
                                case AnnotationValue.StringValue(String string) ->
                                        "the string \"" + string + "\"";
                                case AnnotationValue.ArrayValue(List<AnnotationValue> items) ->
                                        "an array of " + items.size() + " values";
                                default -> element.value().toString();
                            };
                    System.out.println("  " + element.name() + " = " + text);
                }
            }

            for (DexField field : annotated.fields()) {
                if (field.name().equals("MAGIC") || field.name().equals("GREETING")) {
                    System.out.println(field.name() + " = " + field.initialValue().orElseThrow());
                }
            }
        }
        // --8<-- [end:annotations]
    }

    static void strings() throws IOException {
        // --8<-- [start:strings]
        try (AnalysisContext helloCtx =
                AnalysisContext.fromDex(Path.of("../../tests/data/classes.dex"))) {
            List<String> strings = helloCtx.strings();
            System.out.println(strings.size() + " strings; " + strings.contains("Hello Dex++!!"));
        }
        // --8<-- [end:strings]
    }
}
