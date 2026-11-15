package io.github.burogurama.dexpp;

import static io.github.burogurama.dexpp.TestData.data;
import static io.github.burogurama.dexpp.TestData.dex;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;

/** The call graph, the class hierarchy and the cross-reference index. */
class GraphTest {
    private static CallNode node(CallGraph g, MethodId id) {
        return g.find(id).orElseThrow(() -> new AssertionError("no node " + id));
    }

    /** The class descriptors of some nodes, sorted. */
    private static List<String> classesOf(CallGraph g, List<Integer> nodes) {
        List<String> out = new ArrayList<>();
        for (int i : nodes) {
            out.add(g.node(i).orElseThrow().id().classDescriptor());
        }
        out.sort(null);
        return out;
    }

    private static List<String> namesOf(CallGraph g, List<Integer> nodes) {
        List<String> out = new ArrayList<>();
        for (int i : nodes) {
            out.add(g.node(i).orElseThrow().id().name());
        }
        out.sort(null);
        return out;
    }

    private static boolean sortedWithoutDuplicates(List<Integer> list) {
        for (int i = 1; i < list.size(); i++) {
            if (list.get(i - 1) >= list.get(i)) {
                return false;
            }
        }
        return true;
    }

    @Test
    void callGraph() {
        try (AnalysisContext ctx = dex("polymorphic.dex");
                CallGraph g = ctx.callGraph()) {
            assertFalse(g.isEmpty());
            assertTrue(g.nodeCount() > 0);
            assertTrue(g.edgeCount() > 0);

            CallNode sound = node(g, new MethodId("LAnimal;", "sound", "()Ljava/lang/String;"));
            assertTrue(sound.resolved());
            assertEquals(Optional.empty(), g.find(new MethodId("LNo;", "where", "()V")));

            // The interface call may dispatch to the Dog and Cat overrides. The
            // targets are stored once on the declared node.
            assertEquals(List.of("LCat;", "LDog;"), classesOf(g, g.dispatchTargets(sound.index())));
            CallNode noise =
                    node(g, new MethodId("LKennel;", "noise", "(LAnimal;)Ljava/lang/String;"));
            CallEdge site = null;
            for (int i : g.outEdges(noise.index())) {
                CallEdge e = g.edge(i).orElseThrow();
                if (e.calleeNode() == sound.index()) {
                    site = e;
                }
            }
            assertTrue(site != null);
            assertEquals(EdgeOrigin.DECLARED, site.origin());
            assertEquals(InvokeKind.INTERFACE, site.kind());
            assertEquals(g.dispatchTargets(sound.index()), g.edgeDispatchTargets(site.index()));

            // Pet defines no sound(), so the call site in Pet.describe resolves
            // upward to the abstract Animal.sound.
            List<CallNode> nodes = g.nodes();
            List<CallEdge> edges = g.edges();
            List<CallEdge> inherited = new ArrayList<>();
            for (CallEdge e : edges) {
                if (e.origin() == EdgeOrigin.INHERITED_RESOLUTION
                        && nodes.get(e.callerNode()).id().classDescriptor().equals("LPet;")) {
                    inherited.add(e);
                }
            }
            assertEquals(1, inherited.size());
            CallNode callee = nodes.get(inherited.get(0).calleeNode());
            assertEquals("LAnimal;", callee.id().classDescriptor());
            assertTrue(callee.resolved());
            // A site's dispatch targets hang off its declared edge only.
            assertEquals(List.of(), g.edgeDispatchTargets(inherited.get(0).index()));

            // Every call site has exactly one declared edge.
            Map<List<Integer>, Integer> sites = new HashMap<>();
            for (CallEdge e : edges) {
                if (e.origin() == EdgeOrigin.DECLARED) {
                    sites.merge(List.of(e.callerNode(), e.codeOffset()), 1, Integer::sum);
                }
            }
            for (Map.Entry<List<Integer>, Integer> s : sites.entrySet()) {
                assertEquals(1, s.getValue(), "site " + s.getKey());
            }

            // The reverse direction and the neighbour lists include dispatch.
            CallNode dog = node(g, new MethodId("LDog;", "sound", "()Ljava/lang/String;"));
            assertEquals(
                    List.of("LAnimal;", "LPet;"), classesOf(g, g.dispatchSources(dog.index())));
            assertEquals(
                    List.of("describe", "dogNoise", "noise"), namesOf(g, g.callers(dog.index())));
            assertEquals(
                    List.of("LAnimal;", "LCat;", "LDog;"), classesOf(g, g.callees(noise.index())));

            DexMethod m = g.methodFor(sound.index()).orElseThrow();
            assertEquals("sound", m.name());
            assertFalse(m.hasCode());
        }
    }

    @Test
    void callGraphTablesAndLookups() {
        try (AnalysisContext ctx = dex("polymorphic.dex");
                CallGraph g = ctx.callGraph()) {
            List<CallNode> nodes = g.nodes();
            List<CallEdge> edges = g.edges();
            assertEquals(g.nodeCount(), nodes.size());
            assertEquals(g.edgeCount(), edges.size());
            for (int i = 0; i < nodes.size(); i++) {
                CallNode n = nodes.get(i);
                assertEquals(i, n.index());
                assertEquals(Optional.of(n), g.node(i));
                assertEquals(Optional.of(n), g.find(n.id()));
                Optional<DexMethod> m = g.methodFor(i);
                assertEquals(n.resolved(), m.isPresent(), n.id().toString());
                m.ifPresent(method -> assertEquals(n.id(), method.id()));
            }
            for (int i = 0; i < edges.size(); i++) {
                CallEdge e = edges.get(i);
                assertEquals(i, e.index());
                assertEquals(Optional.of(e), g.edge(i));
                assertTrue(g.outEdges(e.callerNode()).contains(i));
                assertTrue(g.inEdges(e.calleeNode()).contains(i));
            }

            // Indices out of range find nothing.
            int far = nodes.size() + 10;
            assertEquals(Optional.empty(), g.node(far));
            assertEquals(Optional.empty(), g.node(-1));
            assertEquals(Optional.empty(), g.edge(edges.size() + 10));
            assertEquals(Optional.empty(), g.methodFor(far));
            for (List<Integer> list :
                    List.of(
                            g.outEdges(far),
                            g.inEdges(far),
                            g.callers(far),
                            g.callees(far),
                            g.dispatchTargets(far),
                            g.dispatchSources(far),
                            g.edgeDispatchTargets(edges.size() + 10))) {
                assertEquals(List.of(), list);
            }

            // The neighbour lists are sorted and have no duplicates.
            boolean external = false;
            for (CallNode n : nodes) {
                for (List<Integer> list :
                        List.of(
                                g.dispatchTargets(n.index()),
                                g.dispatchSources(n.index()),
                                g.callers(n.index()),
                                g.callees(n.index()))) {
                    assertTrue(sortedWithoutDuplicates(list), list + " of " + n.id());
                }
                external = external || !n.resolved();
            }
            assertTrue(external, "no external node");
        }
    }

    @Test
    void edgesWhereFiltersInTheLibrary() {
        try (AnalysisContext ctx = dex("polymorphic.dex");
                CallGraph g = ctx.callGraph()) {
            List<CallEdge> edges = g.edges();
            EdgeOrigin[] origins = {
                EdgeOrigin.DECLARED, EdgeOrigin.INHERITED_RESOLUTION, null, null
            };
            InvokeKind[] kinds = {null, InvokeKind.VIRTUAL, InvokeKind.DIRECT, null};
            for (int c = 0; c < origins.length; c++) {
                List<Integer> want = new ArrayList<>();
                for (CallEdge e : edges) {
                    if ((origins[c] == null || e.origin() == origins[c])
                            && (kinds[c] == null || e.kind() == kinds[c])) {
                        want.add(e.index());
                    }
                }
                assertEquals(want, g.edgesWhere(origins[c], kinds[c]), origins[c] + " " + kinds[c]);
            }
            assertEquals(
                    g.edgesWhere(EdgeOrigin.DECLARED, null), g.edgesWhere(EdgeOrigin.DECLARED));
            assertEquals(g.edgesWhere(null, InvokeKind.DIRECT), g.edgesWhere(InvokeKind.DIRECT));
            assertFalse(
                    g.edgesWhere(EdgeOrigin.INHERITED_RESOLUTION, InvokeKind.VIRTUAL).isEmpty());
            assertEquals(edges.size(), g.edgesWhere(null, null).size());
        }
    }

    private record Graph(
            List<CallNode> nodes, List<CallEdge> edges, List<List<Integer>> dispatch) {}

    private static Graph build(int threads) throws IOException {
        try (AnalysisContext ctx = AnalysisContext.fromApk(data("multidex.apk"), threads);
                CallGraph g = ctx.callGraph()) {
            List<CallNode> nodes = g.nodes();
            List<List<Integer>> dispatch = new ArrayList<>();
            for (CallNode n : nodes) {
                dispatch.add(g.dispatchTargets(n.index()));
            }
            return new Graph(nodes, g.edges(), dispatch);
        }
    }

    @Test
    void callGraphDoesNotDependOnTheThreadCount() throws IOException {
        Graph one = build(1);
        assertFalse(one.edges().isEmpty());
        assertEquals(one, build(0));
    }

    @Test
    void classHierarchy() {
        try (AnalysisContext ctx = dex("polymorphic.dex");
                ClassHierarchy h = ctx.classHierarchy()) {
            assertFalse(h.isEmpty());
            assertTrue(h.isSubtypeOf("LDog;", "LAnimal;"));
            assertFalse(h.isSubtypeOf("LAnimal;", "LDog;"));
            assertTrue(h.isSubtypeOf("LDog;", "LDog;"));
            assertEquals(List.of("LCat;", "LDog;", "LPet;"), sorted(h.allDescendants("LAnimal;")));
            assertTrue(h.isInterface("LAnimal;"));
            assertFalse(h.isInterface("LDog;"));
            assertFalse(h.isLoaded("Ljava/lang/Object;"));
            assertTrue(h.isLoaded("LDog;"));
            assertEquals(List.of("Ljava/lang/Object;", "LAnimal;"), h.supertypes("LPet;"));
            assertEquals(Optional.of("LPet;"), h.superclassOf("LDog;"));
            assertEquals(Optional.empty(), h.superclassOf("Ljava/lang/Object;"));
            assertEquals(Optional.empty(), h.superclassOf("LNo;"));
            assertEquals(List.of("LAnimal;"), h.directInterfaces("LPet;"));
            assertEquals(List.of("LCat;", "LDog;"), sorted(h.subclasses("LPet;")));
            assertEquals(List.of("LPet;"), h.implementers("LAnimal;"));
            assertEquals(
                    List.of("LAnimal;", "LPet;", "Ljava/lang/Object;"),
                    sorted(h.allSupertypes("LDog;")));
            assertEquals(List.of(), h.subclasses("LNo;"));
            assertEquals(List.of(), h.allDescendants("LNo;"));
            assertFalse(h.isSubtypeOf("LNo;", "LDog;"));
        }
    }

    private static List<String> sorted(List<String> list) {
        List<String> out = new ArrayList<>(list);
        out.sort(null);
        return out;
    }

    @Test
    void xrefs() {
        try (AnalysisContext ctx = dex("classes.dex");
                Xrefs x = ctx.xrefs()) {
            assertFalse(x.isEmpty());
            assertTrue(x.referrerCount() > 0);

            List<StringXref> sites = x.stringRefs("Hello Dex++!!");
            assertEquals(1, sites.size());
            MethodId id = x.referrerId(sites.get(0).referrer()).orElseThrow();
            assertEquals("helloDex", id.name());
            assertEquals(
                    "helloDex", x.referrerMethod(sites.get(0).referrer()).orElseThrow().name());
            List<MethodId> referrers = x.referrers();
            assertEquals(x.referrerCount(), referrers.size());
            assertEquals(id, referrers.get(sites.get(0).referrer()));
            assertEquals(Optional.empty(), x.referrerId(referrers.size()));
            assertEquals(Optional.empty(), x.referrerMethod(referrers.size()));
            assertTrue(x.referencedStrings().contains("Hello Dex++!!"));
            assertEquals(List.of(), x.stringRefs("not in the program"));

            FieldId out = new FieldId("Ljava/lang/System;", "out", "Ljava/io/PrintStream;");
            List<FieldXref> refs = x.fieldRefs(out);
            assertEquals(1, refs.size());
            assertTrue(refs.get(0).isStatic());
            assertFalse(refs.get(0).isWrite());
            assertEquals(refs, x.fieldReads(out));
            assertEquals(List.of(), x.fieldWrites(out));

            MethodId println =
                    new MethodId("Ljava/io/PrintStream;", "println", "(Ljava/lang/String;)V");
            List<MethodXref> calls = x.methodRefs(println);
            assertEquals(1, calls.size());
            assertEquals(InvokeKind.VIRTUAL, calls.get(0).kind());
            assertEquals(
                    List.of(new MethodRefGroup(println, calls)), x.methodRefsByName("println"));
            assertEquals(List.of(), x.methodRefsByName("noSuchName"));
        }
        // Type references, and a string with a NUL character.
        try (AnalysisContext pets = dex("polymorphic.dex");
                Xrefs px = pets.xrefs()) {
            List<TypeXref> types = px.typeRefs("Ljava/lang/StringBuilder;");
            assertFalse(types.isEmpty());
            assertEquals(TypeUseKind.NEW_INSTANCE, types.get(0).kind());
        }
        try (AnalysisContext multi = dex("multistring.dex");
                Xrefs mx = multi.xrefs()) {
            assertEquals(1, mx.stringRefs("test\0embedded").size());
            assertEquals(
                    List.of(), mx.stringRefs("test"), "the NUL character is part of the string");
        }
    }
}
