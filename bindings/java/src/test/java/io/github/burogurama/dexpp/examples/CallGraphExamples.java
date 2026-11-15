package io.github.burogurama.dexpp.examples;

import io.github.burogurama.dexpp.AnalysisContext;
import io.github.burogurama.dexpp.CallEdge;
import io.github.burogurama.dexpp.CallGraph;
import io.github.burogurama.dexpp.CallNode;
import io.github.burogurama.dexpp.CallNodeKind;
import io.github.burogurama.dexpp.DexMethod;
import io.github.burogurama.dexpp.EdgeOrigin;
import io.github.burogurama.dexpp.InvokeKind;
import io.github.burogurama.dexpp.MethodId;

import java.io.IOException;
import java.nio.file.Path;
import java.util.List;
import java.util.Optional;

/**
 * The examples of the guide "Call graph". ExamplesTest runs them. Code outside the markers is not
 * shown: it sets up the variables that the earlier sections of the guide made.
 */
final class CallGraphExamples {
    private CallGraphExamples() {}

    private static final Path POLYMORPHIC = Path.of("../../tests/data/polymorphic.dex");

    // --8<-- [start:graph]
    /** A short name for a node: its class and its method name. */
    static String label(CallGraph graph, int index) {
        MethodId id = graph.node(index).orElseThrow().id();
        return id.classDescriptor() + "->" + id.name();
    }

    static void graph() throws IOException {
        Path path = Path.of("../../tests/data/polymorphic.dex");
        try (AnalysisContext ctx = AnalysisContext.fromDex(path);
                CallGraph graph = ctx.callGraph()) {
            System.out.println(graph.nodeCount() + " methods, " + graph.edgeCount() + " edges");
        }
    }
    // --8<-- [end:graph]

    static void node() throws IOException {
        try (AnalysisContext ctx = AnalysisContext.fromDex(POLYMORPHIC);
                CallGraph graph = ctx.callGraph()) {
            // --8<-- [start:node]
            MethodId noiseId = new MethodId("LKennel;", "noise", "(LAnimal;)Ljava/lang/String;");
            CallNode noise = graph.find(noiseId).orElseThrow();
            System.out.println(label(graph, noise.index()) + " kind: " + noise.kind());

            for (int index : graph.outEdges(noise.index())) {
                CallEdge edge = graph.edge(index).orElseThrow();
                System.out.println("  calls " + label(graph, edge.calleeNode()) + " " + edge.kind()
                        + " at offset " + edge.codeOffset());
            }
            // --8<-- [end:node]
        }
    }

    static void dispatch() throws IOException {
        try (AnalysisContext ctx = AnalysisContext.fromDex(POLYMORPHIC);
                CallGraph graph = ctx.callGraph()) {
            MethodId noiseId = new MethodId("LKennel;", "noise", "(LAnimal;)Ljava/lang/String;");
            CallNode noise = graph.find(noiseId).orElseThrow();
            // --8<-- [start:dispatch]
            MethodId soundId = new MethodId("LAnimal;", "sound", "()Ljava/lang/String;");
            CallNode sound = graph.find(soundId).orElseThrow();

            List<String> targets =
                    graph.dispatchTargets(sound.index()).stream()
                            .map((Integer index) -> label(graph, index))
                            .sorted()
                            .toList();
            List<String> callees =
                    graph.callees(noise.index()).stream()
                            .map((Integer index) -> label(graph, index))
                            .sorted()
                            .toList();
            System.out.println(targets);
            System.out.println(callees);
            // --8<-- [end:dispatch]
        }
    }

    static void callers() throws IOException {
        try (AnalysisContext ctx = AnalysisContext.fromDex(POLYMORPHIC);
                CallGraph graph = ctx.callGraph()) {
            // --8<-- [start:callers]
            MethodId dogSoundId = new MethodId("LDog;", "sound", "()Ljava/lang/String;");
            CallNode dogSound = graph.find(dogSoundId).orElseThrow();

            List<String> callers =
                    graph.callers(dogSound.index()).stream()
                            .map((Integer index) -> label(graph, index))
                            .sorted()
                            .toList();
            System.out.println(callers);
            // --8<-- [end:callers]
        }
    }

    static void external() throws IOException {
        try (AnalysisContext ctx = AnalysisContext.fromDex(POLYMORPHIC);
                CallGraph graph = ctx.callGraph()) {
            // --8<-- [start:external]
            List<CallNode> external =
                    graph.nodes().stream()
                            .filter((CallNode node) -> node.kind() == CallNodeKind.EXTERNAL)
                            .toList();
            System.out.println(
                    external.stream()
                            .map((CallNode node) -> label(graph, node.index()))
                            .sorted()
                            .toList());

            Optional<DexMethod> noMethod = graph.methodFor(external.get(0).index());
            System.out.println(noMethod);
            // --8<-- [end:external]
        }
    }

    static void filter() throws IOException {
        try (AnalysisContext ctx = AnalysisContext.fromDex(POLYMORPHIC);
                CallGraph graph = ctx.callGraph()) {
            // --8<-- [start:filter]
            List<Integer> declared = graph.edgesWhere(EdgeOrigin.DECLARED);
            List<Integer> interfaceCalls = graph.edgesWhere(InvokeKind.INTERFACE);
            System.out.println(declared.size() + " call sites, " + interfaceCalls.size()
                    + " of them invoke-interface");
            // --8<-- [end:filter]
        }
    }

    static void inherited() throws IOException {
        try (AnalysisContext ctx = AnalysisContext.fromDex(POLYMORPHIC);
                CallGraph graph = ctx.callGraph()) {
            // --8<-- [start:inherited]
            for (int index : graph.edgesWhere(EdgeOrigin.INHERITED_RESOLUTION)) {
                CallEdge edge = graph.edge(index).orElseThrow();
                System.out.println(label(graph, edge.callerNode()) + " resolves to "
                        + label(graph, edge.calleeNode()));
            }
            // --8<-- [end:inherited]
        }
    }
}
