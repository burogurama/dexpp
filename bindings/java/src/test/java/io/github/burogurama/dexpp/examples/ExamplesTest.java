package io.github.burogurama.dexpp.examples;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.DynamicTest;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.TestFactory;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Set;
import java.util.TreeSet;
import java.util.stream.Collectors;
import java.util.stream.Stream;

/**
 * Runs the examples that the documentation shows, and checks what they print.
 *
 * <p>The guides include the code of an example between its "--8<-- [start:SECTION]" and "--8<--
 * [end:SECTION]" markers, and its output from src/test/resources/examples/TOPIC.SECTION.txt. The
 * topics and sections have the same names as the Python examples in docs/examples. Each test runs
 * one example and compares what it prints with its output file, so the code and the output on the
 * page are checked.
 *
 * <p>After changing an example, write the output files again with {@code mvn test
 * -Dtest=ExamplesTest -Ddexpp.examples.update=true} and look at the diff.
 *
 * <p>The tests run in bindings/java, so the examples open the test files of the repository by the
 * relative path ../../tests/data. The examples are in their own package, so they can only use the
 * public API, as a program of a user does.
 */
class ExamplesTest {
    /** The code of one example. */
    @FunctionalInterface
    interface Code {
        void run() throws IOException;
    }

    /**
     * One example.
     *
     * @param topic the first part of the output file name, such as "code"
     * @param section the name of the snippet markers and the second part of the output file name
     * @param holder the class whose source file holds the snippet
     * @param code the example itself
     */
    record Example(String topic, String section, Class<?> holder, Code code) {
        String name() {
            return topic + "." + section;
        }
    }

    private static final Path OUTPUT = Path.of("src", "test", "resources", "examples");
    private static final Path SOURCES =
            Path.of("src", "test", "java", "io", "github", "burogurama", "dexpp", "examples");
    private static final boolean UPDATE = Boolean.getBoolean("dexpp.examples.update");

    private static final List<Example> EXAMPLES =
            List.of(
                    new Example("quickstart", "quickstart", QuickstartExamples.class,
                            QuickstartExamples::quickstart),
                    new Example("apk_metadata", "manifest", ApkMetadataExamples.class,
                            ApkMetadataExamples::manifest),
                    new Example("apk_metadata", "components", ApkMetadataExamples.class,
                            ApkMetadataExamples::components),
                    new Example("apk_metadata", "reachable", ApkMetadataExamples.class,
                            ApkMetadataExamples::reachable),
                    new Example("apk_metadata", "resources", ApkMetadataExamples.class,
                            ApkMetadataExamples::resources),
                    new Example("apk_metadata", "variants", ApkMetadataExamples.class,
                            ApkMetadataExamples::variants),
                    new Example("apk_metadata", "text_references", ApkMetadataExamples.class,
                            ApkMetadataExamples::textReferences),
                    new Example("apk_metadata", "signing", ApkMetadataExamples.class,
                            ApkMetadataExamples::signing),
                    new Example("apk_metadata", "entries", ApkMetadataExamples.class,
                            ApkMetadataExamples::entries),
                    new Example("apk_metadata", "memory", ApkMetadataExamples.class,
                            ApkMetadataExamples::memory),
                    new Example("code", "load", CodeExamples.class, CodeExamples::load),
                    new Example("code", "classes", CodeExamples.class, CodeExamples::classes),
                    new Example("code", "methods", CodeExamples.class, CodeExamples::methods),
                    new Example("code", "instructions", CodeExamples.class,
                            CodeExamples::instructions),
                    new Example("code", "typed", CodeExamples.class, CodeExamples::typed),
                    new Example("code", "registers", CodeExamples.class, CodeExamples::registers),
                    new Example("code", "cfg", CodeExamples.class, CodeExamples::cfg),
                    new Example("code", "handlers", CodeExamples.class, CodeExamples::handlers),
                    new Example("code", "try", CodeExamples.class, CodeExamples::tryBlocks),
                    new Example("code", "annotations", CodeExamples.class,
                            CodeExamples::annotations),
                    new Example("code", "strings", CodeExamples.class, CodeExamples::strings),
                    new Example("call_graph", "graph", CallGraphExamples.class,
                            CallGraphExamples::graph),
                    new Example("call_graph", "node", CallGraphExamples.class,
                            CallGraphExamples::node),
                    new Example("call_graph", "dispatch", CallGraphExamples.class,
                            CallGraphExamples::dispatch),
                    new Example("call_graph", "callers", CallGraphExamples.class,
                            CallGraphExamples::callers),
                    new Example("call_graph", "inherited", CallGraphExamples.class,
                            CallGraphExamples::inherited),
                    new Example("call_graph", "external", CallGraphExamples.class,
                            CallGraphExamples::external),
                    new Example("call_graph", "filter", CallGraphExamples.class,
                            CallGraphExamples::filter),
                    new Example("hierarchy_xrefs", "hierarchy", HierarchyXrefsExamples.class,
                            HierarchyXrefsExamples::hierarchy),
                    new Example("hierarchy_xrefs", "method_refs", HierarchyXrefsExamples.class,
                            HierarchyXrefsExamples::methodRefs),
                    new Example("hierarchy_xrefs", "strings", HierarchyXrefsExamples.class,
                            HierarchyXrefsExamples::strings),
                    new Example("hierarchy_xrefs", "pattern", HierarchyXrefsExamples.class,
                            HierarchyXrefsExamples::pattern),
                    new Example("hierarchy_xrefs", "fields", HierarchyXrefsExamples.class,
                            HierarchyXrefsExamples::fields),
                    new Example("hierarchy_xrefs", "types", HierarchyXrefsExamples.class,
                            HierarchyXrefsExamples::types),
                    new Example("edit_dex", "read", EditDexExamples.class, EditDexExamples::read),
                    new Example("edit_dex", "split", EditDexExamples.class, EditDexExamples::split),
                    new Example("edit_dex", "merge", EditDexExamples.class, EditDexExamples::merge),
                    new Example("edit_dex", "rename", EditDexExamples.class,
                            EditDexExamples::rename),
                    new Example("edit_dex", "validate", EditDexExamples.class,
                            EditDexExamples::validate),
                    new Example("edit_dex", "write", EditDexExamples.class, EditDexExamples::write),
                    new Example("edit_dex", "errors", EditDexExamples.class,
                            EditDexExamples::errors),
                    new Example("edit_dex", "invalid", EditDexExamples.class,
                            EditDexExamples::invalid),
                    new Example("edit_dex", "version", EditDexExamples.class,
                            EditDexExamples::version),
                    new Example("concepts", "handles", ConceptsExamples.class,
                            ConceptsExamples::handles),
                    new Example("errors", "load", ErrorsExamples.class, ErrorsExamples::load),
                    new Example("errors", "base", ErrorsExamples.class, ErrorsExamples::base));

    /** Runs an example, and returns what it printed. */
    private static String run(Example example) throws IOException {
        PrintStream saved = System.out;
        ByteArrayOutputStream buffer = new ByteArrayOutputStream();
        System.setOut(new PrintStream(buffer, true, StandardCharsets.UTF_8));
        try {
            example.code().run();
        } finally {
            System.setOut(saved);
        }
        return buffer.toString(StandardCharsets.UTF_8).replace("\r\n", "\n");
    }

    /** Runs an example, and checks what it prints against its output file. */
    private static void check(Example example) throws IOException {
        String printed = run(example);
        Path file = OUTPUT.resolve(example.name() + ".txt");
        if (UPDATE) {
            Files.createDirectories(OUTPUT);
            Files.writeString(file, printed, StandardCharsets.UTF_8);
            return;
        }
        assertTrue(Files.exists(file), "no output file for the example " + example.name());
        // Git on Windows may check the file out with CRLF line ends.
        String expected = Files.readString(file, StandardCharsets.UTF_8).replace("\r\n", "\n");
        assertEquals(expected, printed, "output of the example " + example.name());
    }

    @TestFactory
    Stream<DynamicTest> examples() {
        return EXAMPLES.stream()
                .map((Example example) ->
                        DynamicTest.dynamicTest(example.name(), () -> check(example)));
    }

    /** Each example has its snippet markers in the source file of its class. */
    @Test
    void everyExampleHasItsMarkers() throws IOException {
        for (Example example : EXAMPLES) {
            Path source = SOURCES.resolve(example.holder().getSimpleName() + ".java");
            String text = Files.readString(source, StandardCharsets.UTF_8);
            for (String marker : List.of("start", "end")) {
                String line = "// --8<-- [" + marker + ":" + example.section() + "]";
                assertTrue(text.contains(line), source + " has no line " + line);
            }
        }
    }

    /** Each output file belongs to an example; an update removes the ones that do not. */
    @Test
    void everyOutputFileHasAnExample() throws IOException {
        Set<String> names =
                EXAMPLES.stream()
                        .map((Example example) -> example.name() + ".txt")
                        .collect(Collectors.toSet());
        assertEquals(EXAMPLES.size(), names.size(), "two examples with the same name");
        Set<String> leftover = new TreeSet<>();
        try (Stream<Path> files = Files.list(OUTPUT)) {
            for (Path file : files.toList()) {
                if (!names.contains(file.getFileName().toString())) {
                    leftover.add(file.getFileName().toString());
                }
            }
        }
        if (UPDATE) {
            for (String name : leftover) {
                Files.delete(OUTPUT.resolve(name));
            }
            return;
        }
        assertTrue(leftover.isEmpty(), "output files with no example: " + leftover);
    }
}
