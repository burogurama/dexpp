package io.github.burogurama.dexpp;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.List;
import java.util.Optional;

/**
 * The call graph of the whole program: which method can call which. Get it from {@link
 * AnalysisContext#callGraph()}.
 *
 * <p>Nodes and edges are numbered. A {@link CallNode} or a {@link CallEdge} is a copy that holds
 * the numbers of its neighbours, and the methods here take and return these numbers. {@link
 * #nodes()} and {@link #edges()} copy the whole table in one call: call them once and index the
 * list. {@link #node(int)} and {@link #edge(int)} copy one entry, which is cheaper when only a few
 * are needed.
 *
 * <p>Virtual dispatch is not stored as edges. A call site has one {@link EdgeOrigin#DECLARED} edge,
 * to the method the bytecode names, and at most one {@link EdgeOrigin#INHERITED_RESOLUTION} edge.
 * The methods a virtual or interface call may also run are the dispatch targets of the declared
 * callee: see {@link #dispatchTargets(int)}. {@link #callees(int)} and {@link #callers(int)}
 * include them.
 *
 * <p>It is a handle: {@link #close()} frees it, and a {@link java.lang.ref.Cleaner} frees it when
 * it is no longer used. The graph itself stays in the analysis, so {@link
 * AnalysisContext#callGraph()} returns it again without building it again.
 */
public final class CallGraph implements AutoCloseable {
    private final NativeHandle handle;

    CallGraph(MemorySegment pointer) {
        handle = new NativeHandle("CallGraph", pointer, Native::dexpp_call_graph_free);
    }

    /** Frees the handle at once. A second call does nothing. */
    @Override
    public void close() {
        handle.close();
    }

    private static CallNode node(MemorySegment m, long b) {
        return new CallNode(
                Mem.i32(m, b + Layouts.CallNode.INDEX),
                Mem.methodId(m, b + Layouts.CallNode.ID),
                Mem.bool(m, b + Layouts.CallNode.RESOLVED));
    }

    private static CallEdge edge(int index, MemorySegment m, long b) {
        return new CallEdge(
                index,
                Mem.i32(m, b + Layouts.CallEdge.CALLER_NODE),
                Mem.i32(m, b + Layouts.CallEdge.CALLEE_NODE),
                InvokeKind.of(Mem.i32(m, b + Layouts.CallEdge.KIND)),
                Mem.i32(m, b + Layouts.CallEdge.CODE_OFFSET),
                EdgeOrigin.of(Mem.i32(m, b + Layouts.CallEdge.ORIGIN)));
    }

    /**
     * The number of nodes.
     *
     * @return the node count
     */
    public int nodeCount() {
        return Math.toIntExact(handle.callLong(Native::dexpp_call_graph_node_count));
    }

    /**
     * The number of edges.
     *
     * @return the edge count
     */
    public int edgeCount() {
        return Math.toIntExact(handle.callLong(Native::dexpp_call_graph_edge_count));
    }

    /**
     * Tells whether the graph has no nodes.
     *
     * @return true for an empty graph
     */
    public boolean isEmpty() {
        return nodeCount() == 0;
    }

    /**
     * Every node, in index order, so {@code nodes().get(i).index() == i}. It copies the whole
     * table: call it once, not in a loop.
     *
     * @return the nodes
     */
    public List<CallNode> nodes() {
        return handle.call(g ->
                Mem.take(
                        (arena, count) -> Native.dexpp_call_graph_nodes(g, count),
                        Layouts.CallNode.LAYOUT,
                        CallGraph::node,
                        Native::dexpp_call_node_array_free));
    }

    /**
     * The node with this index.
     *
     * @param index a node index
     * @return the node, or empty when the index is out of range
     */
    public Optional<CallNode> node(int index) {
        return handle.call(g -> {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment out = arena.allocate(Layouts.CallNode.LAYOUT);
                if (Native.dexpp_call_graph_node(g, index, out) == 0) {
                    return Optional.empty();
                }
                try {
                    return Optional.of(node(out, 0));
                } finally {
                    Native.dexpp_call_node_clear(out);
                }
            }
        });
    }

    /**
     * Every edge, in index order, so {@code edges().get(i).index() == i}. It copies the whole
     * table: call it once, not in a loop.
     *
     * @return the edges
     */
    public List<CallEdge> edges() {
        return handle.call(g ->
                Mem.take(
                        (arena, count) -> Native.dexpp_call_graph_edges(g, count),
                        Layouts.CallEdge.LAYOUT,
                        (m, b) ->
                                edge(Math.toIntExact(b / Layouts.CallEdge.LAYOUT.byteSize()), m, b),
                        Native::dexpp_call_edge_array_free));
    }

    /**
     * The edge with this index.
     *
     * @param index an edge index
     * @return the edge, or empty when the index is out of range
     */
    public Optional<CallEdge> edge(int index) {
        return handle.call(g -> {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment out = arena.allocate(Layouts.CallEdge.LAYOUT);
                if (Native.dexpp_call_graph_edge(g, index, out) == 0) {
                    return Optional.empty();
                }
                return Optional.of(edge(index, out, 0));
            }
        });
    }

    /**
     * The node of the method with this identity.
     *
     * @param id the method's identity
     * @return the node, or empty when the graph has no such method
     */
    public Optional<CallNode> find(MethodId id) {
        long index =
                handle.callLong(g -> {
                    try (Arena arena = Arena.ofConfined()) {
                        return Native.dexpp_call_graph_find(
                                g,
                                Mem.cString(arena, id.classDescriptor()),
                                Mem.cString(arena, id.name()),
                                Mem.cString(arena, id.proto()));
                    }
                });
        return index < 0 ? Optional.empty() : node(Math.toIntExact(index));
    }

    /**
     * The indices of the edges that leave the node: the call sites in this method.
     *
     * @param node a node index
     * @return the edge indices; empty when the index is out of range
     */
    public List<Integer> outEdges(int node) {
        return handle.call(g ->
                Mem.takeIndices(
                        (arena, count) -> Native.dexpp_call_graph_out_edges(g, node, count)));
    }

    /**
     * The indices of the edges that enter the node: the calls into this method.
     *
     * @param node a node index
     * @return the edge indices; empty when the index is out of range
     */
    public List<Integer> inEdges(int node) {
        return handle.call(g ->
                Mem.takeIndices(
                        (arena, count) -> Native.dexpp_call_graph_in_edges(g, node, count)));
    }

    /**
     * The other methods that a virtual or interface call declared against this node may run: the
     * overrides in loaded subclasses and implementers, and what subclasses that do not override it
     * inherit. The list is sorted by index and stored once on the node, shared by all its call
     * sites. Every target is a resolved node.
     *
     * @param node a node index
     * @return the node indices; empty when no such call names this method
     */
    public List<Integer> dispatchTargets(int node) {
        return handle.call(g ->
                Mem.takeIndices((arena, count) ->
                        Native.dexpp_call_graph_dispatch_targets(g, node, count)));
    }

    /**
     * The reverse of {@link #dispatchTargets(int)}: the declared methods through which a virtual or
     * interface call can reach this node.
     *
     * @param node a node index
     * @return the node indices
     */
    public List<Integer> dispatchSources(int node) {
        return handle.call(g ->
                Mem.takeIndices((arena, count) ->
                        Native.dexpp_call_graph_dispatch_sources(g, node, count)));
    }

    /**
     * Every method this node may call: the callee of each outgoing edge, plus the dispatch targets
     * of each virtual and interface call. The list is sorted and has no duplicates.
     *
     * @param node a node index
     * @return the node indices
     */
    public List<Integer> callees(int node) {
        return handle.call(g ->
                Mem.takeIndices((arena, count) -> Native.dexpp_call_graph_callees(g, node, count)));
    }

    /**
     * Every method that may call this node, directly or through virtual dispatch. The list is
     * sorted and has no duplicates.
     *
     * @param node a node index
     * @return the node indices
     */
    public List<Integer> callers(int node) {
        return handle.call(g ->
                Mem.takeIndices((arena, count) -> Native.dexpp_call_graph_callers(g, node, count)));
    }

    /**
     * The dispatch targets that apply at one edge: those of its callee for a {@link
     * EdgeOrigin#DECLARED} edge of an {@code invoke-virtual} or {@code invoke-interface}, and none
     * for every other edge.
     *
     * @param edge an edge index
     * @return the node indices
     */
    public List<Integer> edgeDispatchTargets(int edge) {
        return handle.call(g ->
                Mem.takeIndices((arena, count) ->
                        Native.dexpp_call_graph_edge_dispatch_targets(g, edge, count)));
    }

    /**
     * The indices of the edges that match both filters. The filtering runs in the library, which is
     * much faster than copying every edge into Java first. {@code edgesWhere(EdgeOrigin.DECLARED,
     * null)} gives exactly one edge per call site.
     *
     * @param origin the origin to keep, or null for any
     * @param kind the invoke kind to keep, or null for any
     * @return the edge indices, in index order
     */
    public List<Integer> edgesWhere(EdgeOrigin origin, InvokeKind kind) {
        int o = origin == null ? -1 : origin.value();
        int k = kind == null ? -1 : kind.value();
        return handle.call(g ->
                Mem.takeIndices(
                        (arena, count) -> Native.dexpp_call_graph_edges_where(g, o, k, count)));
    }

    /**
     * The edges with this origin, of any invoke kind.
     *
     * @param origin the origin to keep
     * @return the edge indices, in index order
     */
    public List<Integer> edgesWhere(EdgeOrigin origin) {
        return edgesWhere(origin, null);
    }

    /**
     * The edges with this invoke kind, of any origin.
     *
     * @param kind the invoke kind to keep
     * @return the edge indices, in index order
     */
    public List<Integer> edgesWhere(InvokeKind kind) {
        return edgesWhere(null, kind);
    }

    /**
     * The method behind a resolved node.
     *
     * @param node a node index
     * @return the method, or empty for an external node and for an index out of range
     */
    public Optional<DexMethod> methodFor(int node) {
        return handle.call(g -> {
            MemorySegment m = Native.dexpp_call_graph_method_for(g, node);
            return Mem.isNull(m) ? Optional.empty() : Optional.of(new DexMethod(m));
        });
    }
}
