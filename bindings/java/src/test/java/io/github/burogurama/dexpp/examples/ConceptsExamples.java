package io.github.burogurama.dexpp.examples;

import io.github.burogurama.dexpp.AnalysisContext;
import io.github.burogurama.dexpp.DexClass;
import io.github.burogurama.dexpp.DexMethod;

import java.io.IOException;
import java.nio.file.Path;
import java.util.List;

/** The examples of the concepts page. ExamplesTest runs them. */
final class ConceptsExamples {
    private ConceptsExamples() {}

    // --8<-- [start:handles]
    /** Loads a DEX file and returns one of its classes. */
    static DexClass mainClass(Path path) throws IOException {
        try (AnalysisContext ctx = AnalysisContext.fromDex(path)) {
            return ctx.findClass("LTestDex;").orElseThrow();
        } // the context is closed here
    }

    static void handles() throws IOException {
        DexClass cls = mainClass(Path.of("../../tests/data/classes.dex"));
        // The class still works after its context is closed.
        List<String> methodNames = cls.methods().stream().map(DexMethod::name).toList();
        System.out.println(cls.descriptor() + " " + methodNames);
    }
    // --8<-- [end:handles]
}
