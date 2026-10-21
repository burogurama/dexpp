#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/ids.hpp" // MethodId, MethodIdHash

namespace dex {

class Method;
namespace detail {
struct AnalysisContextImpl;
} // namespace detail

/** Kind of invoke instruction at a call site, derived from the opcode. */
enum class InvokeKind : uint8_t {
    Virtual,     ///< invoke-virtual / invoke-virtual/range          (0x6e, 0x74)
    Super,       ///< invoke-super / invoke-super/range              (0x6f, 0x75)
    Direct,      ///< invoke-direct / invoke-direct/range            (0x70, 0x76)
    Static,      ///< invoke-static / invoke-static/range            (0x71, 0x77)
    Interface,   ///< invoke-interface / invoke-interface/range      (0x72, 0x78)
    Polymorphic, ///< invoke-polymorphic / invoke-polymorphic/range  (0xfa, 0xfb)
    Custom,      ///< invoke-custom / invoke-custom/range            (0xfc, 0xfd)
};

/** Map a Dalvik invoke opcode to its InvokeKind.  Returns std::nullopt for any
 *  opcode that is not a recognised invoke. */
std::optional<InvokeKind> invoke_kind_from_opcode(uint8_t opcode);

/** How an edge came to point at its callee. */
enum class EdgeOrigin : uint8_t {
    /** The invoke's literal declared target (the method_id in the bytecode).
     *  Exactly one per call site. */
    Declared,
    /** Upward resolution of the declared method_id: the declared class defines
     *  no matching method, and this edge points at the nearest definition a
     *  loaded ancestor (superclass chain, then superinterfaces) provides —
     *  what Dalvik resolution finds at runtime.  At most one per call site. */
    InheritedResolution,
};

/** A single call-site edge from one method to another.
 *
 *  A call site has one Declared edge and at most one InheritedResolution edge;
 *  both share `caller_node`, `kind`, and `code_offset`.  The site's possible
 *  callees — those edges' callees plus `CallGraph::dispatch_targets(edge)` —
 *  are pairwise distinct, and every one except the Declared callee is a
 *  resolved node. */
struct CallEdge
{
    uint32_t caller_node; ///< Index into CallGraph::nodes().
    uint32_t callee_node; ///< Index into CallGraph::nodes().
    InvokeKind kind;
    uint32_t code_offset; ///< Code-unit offset of the invoke within the caller's code_item.
    EdgeOrigin origin = EdgeOrigin::Declared;
};

/** A node in the call graph.  Represents either a defined method (resolved=true)
 *  or an external reference (resolved=false) that some defined method calls into.
 *
 *  Custom call sites are represented as resolved=false nodes with a synthetic
 *  MethodId of the form `{ "call_site", "#<dex_idx>:<call_site_index>", "" }`. */
struct CallNode
{
    uint32_t index; ///< Position of this node in CallGraph::nodes().
    MethodId id;
    bool resolved;                  ///< true iff `id` matches a method defined in some loaded DEX.
    std::vector<uint32_t> outgoing; ///< Edge indices into CallGraph::edges().
    std::vector<uint32_t> incoming; ///< Edge indices into CallGraph::edges().
};

/** Whole-program call graph for an AnalysisContext.
 *
 *  Built lazily on first `AnalysisContext::call_graph()` access and cached on
 *  the underlying impl.  Edges are stored in a single flat vector.  Each invoke
 *  instruction yields exactly one declared-target edge (`origin == Declared`),
 *  uniquely traceable via its `code_offset`, plus at most one
 *  InheritedResolution edge sharing that offset and kind (see below).
 *
 *  Virtual dispatch uses Class-Hierarchy Analysis: an invoke-virtual /
 *  invoke-interface call site may also reach every distinct dispatch target
 *  among the declared class's loaded descendants — a descendant's own
 *  override, or, for descendants that don't override, the definition they
 *  inherit (which may live outside the declared class's subtree, e.g.
 *  `class C extends Base implements I` where `Base` provides `I`'s method).
 *  Static / private / constructor name collisions are never dispatch targets
 *  and are resolved past.  These targets depend only on the declared node,
 *  never on the call site, so they are not stored as edges: `dispatch_targets()`
 *  holds them once per declared node and every call site shares that list.
 *  (Stored per call site, a popular target such as `Object.toString()` would
 *  repeat its whole override list at every call — on large apps that is
 *  hundreds of millions of edges.)  `callees()` / `callers()` give a node's
 *  neighbours with dispatch included.
 *
 *  When the declared class itself defines no matching method, the call site
 *  additionally gets one InheritedResolution edge to the nearest loaded
 *  definition, mirroring Dalvik resolution: the superclass chain first
 *  (nearest-first; an abstract redeclaration shadows concrete ancestors), then
 *  the transitive superinterfaces of every chain class, preferring default
 *  methods over abstract declarations.  Applies to virtual, interface, super,
 *  and static invokes (static resolves through the superclass chain only —
 *  static interface methods are not inherited).  If no loaded ancestor defines
 *  the method (e.g. the chain leaves the loaded set at a framework class), the
 *  declared node simply stays unresolved and no edge is added.
 *
 *  Known divergences from runtime resolution, chosen for analysis usefulness:
 *  flag-mismatched shadows (e.g. a static hiding an instance method) are
 *  skipped rather than resolved-then-ICCE; conflicting maximally-specific
 *  default methods pick the BFS-nearest where the runtime throws; an abstract
 *  redeclaration that would raise AbstractMethodError still resolves to the
 *  nearest default/abstract definition; invoke-super resolves from the
 *  method_id's class (identical to the caller's superclass in compiler
 *  output); Object methods called through an interface, and array-receiver
 *  descriptors, stay unresolved (Object is never loaded).
 *
 *  invoke-super never dispatches.  invoke-polymorphic records the declared
 *  MethodHandle target only — distinct call-site shapes through the same
 *  MethodHandle.invoke collapse into one node.
 *
 *  Duplicate definitions (same MethodId in multiple loaded DEX files) follow
 *  Android's last-loaded-wins rule: `method_for()` resolves to the most
 *  recently registered definition.
 *
 *  Move-only: copying a graph would deep-copy all node/edge vectors. */
class CallGraph
{
  public:
    CallGraph() = default;
    CallGraph(const CallGraph &) = delete;
    CallGraph &operator=(const CallGraph &) = delete;
    CallGraph(CallGraph &&) = default;
    CallGraph &operator=(CallGraph &&) = default;

    std::span<const CallNode> nodes() const { return nodes_; }
    std::span<const CallEdge> edges() const { return edges_; }

    /** O(1) lookup of a node by its canonical MethodId.  Returns nullptr if no
     *  node carries that id. */
    const CallNode *find(const MethodId &id) const;

    /** If `node.resolved` is true, returns a Method handle for the underlying
     *  defined method.  Returns nullopt for unresolved (external or custom)
     *  nodes, or if the owning AnalysisContext has been destroyed.
     *  Precondition: `node` must be a member of this graph's `nodes()` span. */
    std::optional<Method> method_for(const CallNode &node) const;

    /** Class-Hierarchy Analysis targets of a virtual / interface call declared
     *  against node index @p node: the definitions such a call may dispatch to
     *  at runtime besides `node` itself and its own upward resolution.  Every
     *  target is a resolved node; sorted by node index.  Empty when no
     *  invoke-virtual / invoke-interface site declares `node`, when no loaded
     *  descendant changes the dispatch, or when `node` is out of range. */
    std::span<const uint32_t> dispatch_targets(uint32_t node) const;

    /** The dispatch targets that apply at one call site:
     *  `dispatch_targets(edge.callee_node)` for a Declared invoke-virtual /
     *  invoke-interface edge, and empty for every other edge — super, direct,
     *  static, polymorphic, and custom invokes never dispatch, and an
     *  InheritedResolution edge's site is covered by its Declared edge. */
    std::span<const uint32_t> dispatch_targets(const CallEdge &edge) const;

    /** The reverse of dispatch_targets(): the nodes whose dispatch targets
     *  include node index @p node — the declared targets through which a
     *  virtual / interface call can dispatch to it.  Sorted by node index;
     *  empty when `node` is out of range. */
    std::span<const uint32_t> dispatch_sources(uint32_t node) const;

    /** Every node that a call site in node index @p node may reach: each
     *  outgoing edge's callee plus the dispatch targets of each virtual /
     *  interface site.  Deduplicated and sorted by node index; empty when
     *  `node` is out of range. */
    std::vector<uint32_t> callees(uint32_t node) const;

    /** Every node with a call site that may reach node index @p node: the
     *  callers on its incoming edges, plus the callers of every virtual /
     *  interface site declared against one of its dispatch_sources().
     *  Deduplicated and sorted by node index; empty when `node` is out of
     *  range. */
    std::vector<uint32_t> callers(uint32_t node) const;

    bool empty() const { return nodes_.empty(); }

  private:
    friend CallGraph build_call_graph(std::shared_ptr<const detail::AnalysisContextImpl>);

    /** Back-reference to the defined method behind a resolved node, used by
     *  method_for() to construct a Method handle on demand. */
    struct ResolvedRef
    {
        std::size_t dex_idx;
        uint32_t method_abs_idx;
        uint32_t access_flags;
        uint32_t code_off;
    };

    std::vector<CallNode> nodes_;
    std::vector<CallEdge> edges_;
    /** find()'s lookup table: open addressing over MethodIdHash, each slot a
     *  node index + 1 (0 = empty).  Probes compare against nodes_[i].id, so the
     *  ids are stored once, in nodes_. */
    std::vector<uint32_t> index_;
    std::vector<std::optional<ResolvedRef>> resolved_refs_; ///< parallel to nodes_
    std::weak_ptr<const detail::AnalysisContextImpl> impl_;

    // dispatch_targets() / dispatch_sources() as compressed rows: node n's
    // entries are values[offsets[n] .. offsets[n + 1]).  The offsets hold
    // nodes_.size() + 1 entries, or none for a graph built without a context.
    std::vector<std::size_t> dispatch_target_offsets_;
    std::vector<uint32_t> dispatch_target_values_;
    std::vector<std::size_t> dispatch_source_offsets_;
    std::vector<uint32_t> dispatch_source_values_;
};

/** Build the whole-program call graph from an analysis-context implementation.
 *  Used internally by AnalysisContext::call_graph()'s lazy cache. */
CallGraph build_call_graph(std::shared_ptr<const detail::AnalysisContextImpl> impl);

} // namespace dex
