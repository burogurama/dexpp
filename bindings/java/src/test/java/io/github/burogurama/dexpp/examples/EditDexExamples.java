package io.github.burogurama.dexpp.examples;

import io.github.burogurama.dexpp.ClassInfo;
import io.github.burogurama.dexpp.InvalidDexException;
import io.github.burogurama.dexpp.InvalidModelException;
import io.github.burogurama.dexpp.Model;
import io.github.burogurama.dexpp.PoolStats;
import io.github.burogurama.dexpp.Problem;
import io.github.burogurama.dexpp.VersionTooLowException;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.stream.Stream;

/**
 * The examples of the guide "Editing DEX files". ExamplesTest runs them. Code outside the markers
 * is not shown: it sets up the variables that the earlier sections of the guide made.
 */
final class EditDexExamples {
    private EditDexExamples() {}

    private static final Path SHAPES = Path.of("../../tests/data/shapes.dex");

    /** Writes one DEX file per class of the model into a folder, and returns their names. */
    private static List<String> splitInto(Model model, Path dir) throws IOException {
        List<String> names = new ArrayList<>();
        List<String> descriptors = model.classDescriptors();
        for (int index = 0; index < descriptors.size(); index++) {
            try (Model one = new Model(model.version())) {
                one.copyClass(model, descriptors.get(index));
                String name = "class" + index + ".dex";
                one.writeFile(dir.resolve(name));
                names.add(name);
            }
        }
        return names;
    }

    /** Deletes a folder and the files in it. */
    private static void deleteFolder(Path dir) throws IOException {
        try (Stream<Path> files = Files.list(dir)) {
            for (Path file : files.toList()) {
                Files.delete(file);
            }
        }
        Files.delete(dir);
    }

    static void read() throws IOException {
        // --8<-- [start:read]
        try (Model model = Model.readFile(Path.of("../../tests/data/shapes.dex"))) {
            System.out.println("DEX version " + model.version() + " with " + model.classCount()
                    + " classes");
            System.out.println(model.classDescriptors());

            ClassInfo info = model.classInfo("LCircle;").orElseThrow();
            System.out.println(info.descriptor() + " extends " + info.superclass().orElse("nothing")
                    + " - " + info.methodCount() + " methods");

            ClassInfo abstractShape = model.classInfo("LAbstractShape;").orElseThrow();
            System.out.println(abstractShape.descriptor() + " implements "
                    + abstractShape.interfaces());
        }
        // --8<-- [end:read]
    }

    static void split() throws IOException {
        try (Model model = Model.readFile(SHAPES)) {
            // --8<-- [start:split]
            Path outDir = Files.createTempDirectory("dexpp");
            List<String> descriptors = model.classDescriptors();
            for (int index = 0; index < descriptors.size(); index++) {
                try (Model one = new Model(model.version())) {
                    one.copyClass(model, descriptors.get(index));
                    one.writeFile(outDir.resolve("class" + index + ".dex"));
                }
            }

            try (Stream<Path> files = Files.list(outDir)) {
                List<String> fileNames =
                        files.map((Path file) -> file.getFileName().toString()).sorted().toList();
                System.out.println(fileNames);
            }
            // --8<-- [end:split]
            deleteFolder(outDir);
        }
    }

    static void merge() throws IOException {
        Path outDir = Files.createTempDirectory("dexpp");
        try (Model model = Model.readFile(SHAPES)) {
            List<String> fileNames = splitInto(model, outDir);
            // --8<-- [start:merge]
            try (Model merged = new Model(model.version())) {
                for (String fileName : fileNames) {
                    try (Model part = Model.readFile(outDir.resolve(fileName))) {
                        for (String descriptor : part.classDescriptors()) {
                            merged.moveClass(part, descriptor);
                        }
                    }
                }

                System.out.println(merged.classCount() + " classes");
                boolean same = Arrays.equals(merged.write(), model.write());
                System.out.println("same bytes as the original model: " + same);
            }
            // --8<-- [end:merge]
        } finally {
            deleteFolder(outDir);
        }
    }

    static void rename() throws IOException {
        try (Model model = Model.readFile(SHAPES)) {
            // --8<-- [start:rename]
            Map<String, String> renames = Map.of("LCircle;", "Lcom/example/Round;");
            model.renameTypes(renames);
            System.out.println(model.classDescriptors());
            // --8<-- [end:rename]
        }
    }

    static void validate() throws IOException {
        try (Model model = Model.readFile(SHAPES)) {
            model.renameTypes(Map.of("LCircle;", "Lcom/example/Round;"));
            // --8<-- [start:validate]
            List<Problem> problems = model.validate();
            System.out.println("problems: " + problems);

            PoolStats stats = model.poolStats();
            System.out.println(stats.strings() + " strings, " + stats.types() + " types, "
                    + stats.methods() + " methods");
            int needed = model.requiredVersion();
            System.out.println("needs DEX version " + needed);
            // --8<-- [end:validate]
        }
    }

    static void write() throws IOException {
        try (Model model = Model.readFile(SHAPES)) {
            model.renameTypes(Map.of("LCircle;", "Lcom/example/Round;"));
            // --8<-- [start:write]
            byte[] data = model.write();
            try (Model again = Model.read(data)) {
                boolean repeatable = Arrays.equals(again.write(), data);
                System.out.println(data.length + " bytes; writing is repeatable: " + repeatable);
            }
            // --8<-- [end:write]
        }
    }

    static void errors() {
        // --8<-- [start:errors]
        try (Model model = Model.read("not a dex file".getBytes(StandardCharsets.UTF_8))) {
            System.out.println(model.classCount());
        } catch (InvalidDexException e) {
            System.out.println("InvalidDexException: " + e.getMessage());
        }
        // --8<-- [end:errors]
    }

    static void invalid() throws IOException {
        // --8<-- [start:invalid]
        try (Model broken = Model.readFile(Path.of("../../tests/data/shapes.dex"))) {
            broken.renameTypes(Map.of("LCircle;", "not a descriptor"));
            try {
                broken.write();
            } catch (InvalidModelException e) {
                System.out.println(e.problems().size() + " problems");
                for (Problem problem : e.problems()) {
                    System.out.println("  " + problem.where() + " - " + problem.message());
                }
            }
        }
        // --8<-- [end:invalid]
    }

    static void version() throws IOException {
        // --8<-- [start:version]
        try (Model defaults = Model.readFile(Path.of("../../tests/data/inherit.dex"))) {
            defaults.setVersion(35);
            try {
                defaults.write();
            } catch (VersionTooLowException e) {
                System.out.println("VersionTooLowException: " + e.getMessage());
            }

            defaults.setVersion(defaults.requiredVersion());
            System.out.println("written with version " + defaults.version() + " - "
                    + defaults.write().length + " bytes");
        }
        // --8<-- [end:version]
    }
}
