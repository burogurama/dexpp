package io.github.burogurama.dexpp.examples;

import io.github.burogurama.dexpp.AnalysisContext;
import io.github.burogurama.dexpp.ClassHierarchy;
import io.github.burogurama.dexpp.DexMethod;
import io.github.burogurama.dexpp.FieldId;
import io.github.burogurama.dexpp.FieldXref;
import io.github.burogurama.dexpp.MethodId;
import io.github.burogurama.dexpp.MethodRefGroup;
import io.github.burogurama.dexpp.MethodXref;
import io.github.burogurama.dexpp.StringXref;
import io.github.burogurama.dexpp.TypeXref;
import io.github.burogurama.dexpp.Xrefs;

import java.io.IOException;
import java.nio.file.Path;
import java.util.List;
import java.util.regex.Pattern;

/**
 * The examples of the guide "Class hierarchy and cross-references". ExamplesTest runs them. Code
 * outside the markers is not shown: it sets up the variables that the earlier sections of the guide
 * made.
 */
final class HierarchyXrefsExamples {
    private HierarchyXrefsExamples() {}

    private static final Path CLASSES = Path.of("../../tests/data/classes.dex");

    static void hierarchy() throws IOException {
        // --8<-- [start:hierarchy]
        Path path = Path.of("../../tests/data/shapes.dex");
        try (AnalysisContext shapes = AnalysisContext.fromDex(path);
                ClassHierarchy hierarchy = shapes.classHierarchy()) {
            List<String> supertypes = hierarchy.supertypes("LCircle;");
            List<String> ancestors = hierarchy.allSupertypes("LCircle;");
            List<String> descendants =
                    hierarchy.allDescendants("LShape;").stream().sorted().toList();
            System.out.println(supertypes);
            System.out.println(ancestors);
            System.out.println(descendants);
            System.out.println(hierarchy.implementers("LShape;"));
            System.out.println(hierarchy.subclasses("LAbstractShape;").stream().sorted().toList());

            boolean isShape = hierarchy.isSubtypeOf("LCircle;", "LShape;");
            System.out.println(isShape + " " + hierarchy.isInterface("LShape;"));
            System.out.println(hierarchy.isLoaded("Ljava/lang/Object;"));
        }
        // --8<-- [end:hierarchy]
    }

    static void methodRefs() throws IOException {
        // --8<-- [start:method_refs]
        Path path = Path.of("../../tests/data/classes.dex");
        try (AnalysisContext ctx = AnalysisContext.fromDex(path);
                Xrefs xrefs = ctx.xrefs()) {
            for (MethodRefGroup group : xrefs.methodRefsByName("println")) {
                for (MethodXref site : group.sites()) {
                    MethodId caller = xrefs.referrerId(site.referrer()).orElseThrow();
                    String called = group.target().classDescriptor() + " " + group.target().name();
                    System.out.println(called + " is called by " + caller.name()
                            + " at " + site.codeOffset());
                }
            }
        }
        // --8<-- [end:method_refs]
    }

    static void strings() throws IOException {
        try (AnalysisContext ctx = AnalysisContext.fromDex(CLASSES);
                Xrefs xrefs = ctx.xrefs()) {
            // --8<-- [start:strings]
            List<String> loaded = xrefs.referencedStrings();
            System.out.println(loaded);

            for (StringXref site : xrefs.stringRefs("Hello Dex++!!")) {
                DexMethod method = xrefs.referrerMethod(site.referrer()).orElseThrow();
                System.out.println("loaded in " + method.declaringClass().descriptor() + " "
                        + method.name());
            }
            // --8<-- [end:strings]
        }
    }

    static void pattern() throws IOException {
        // --8<-- [start:pattern]
        Path multistring = Path.of("../../tests/data/multistring.dex");
        try (AnalysisContext greetingsCtx = AnalysisContext.fromDex(multistring);
                Xrefs greetings = greetingsCtx.xrefs()) {
            Pattern world = Pattern.compile("World!$");
            // The strings come in no fixed order, so sort them.
            List<String> values = greetings.referencedStrings().stream().sorted().toList();
            for (String value : values) {
                if (world.matcher(value).find()) {
                    for (StringXref site : greetings.stringRefs(value)) {
                        MethodId loader = greetings.referrerId(site.referrer()).orElseThrow();
                        System.out.println("\"" + value + "\" loaded by " + loader.name()
                                + " at " + site.codeOffset());
                    }
                }
            }
        }
        // --8<-- [end:pattern]
    }

    static void fields() throws IOException {
        try (AnalysisContext ctx = AnalysisContext.fromDex(CLASSES);
                Xrefs xrefs = ctx.xrefs()) {
            // --8<-- [start:fields]
            FieldId out = new FieldId("Ljava/lang/System;", "out", "Ljava/io/PrintStream;");
            List<FieldXref> reads = xrefs.fieldReads(out);
            List<FieldXref> writes = xrefs.fieldWrites(out);
            System.out.println(reads.size() + " reads, " + writes.size() + " writes");
            // --8<-- [end:fields]
        }
    }

    static void types() throws IOException {
        // --8<-- [start:types]
        Path polymorphic = Path.of("../../tests/data/polymorphic.dex");
        try (AnalysisContext petsCtx = AnalysisContext.fromDex(polymorphic);
                Xrefs pets = petsCtx.xrefs()) {
            for (TypeXref site : pets.typeRefs("Ljava/lang/StringBuilder;")) {
                MethodId user = pets.referrerId(site.referrer()).orElseThrow();
                System.out.println(site.kind() + " in " + user.classDescriptor() + " "
                        + user.name() + " at " + site.codeOffset());
            }
        }
        // --8<-- [end:types]
    }
}
