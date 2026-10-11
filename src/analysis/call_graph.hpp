#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace dex {

class Method;
namespace detail {
struct AnalysisContextImpl;
} // namespace detail

/** Canonical, DEX-portable identity for a method reference.
 *
 *  The triple (declaring class descriptor, name, full prototype descriptor)
 *  uniquely identifies a method across all loaded DEX files.  Owned strings —
 *  safe to store, log, and compare without lifetime concerns.
 *
 *  The `proto` field is the full descriptor (e.g. "(Ljava/lang/String;)V"),
 *  not the shorty.  Shorty collapses every reference type to 'L' and would
 *  collide for overloads such as `f(String)V` vs `f(Object)V`. */
struct MethodId
{
    std::string class_descriptor; ///< 'L...;' form, e.g. "Ljava/lang/Object;"
    std::string name;             ///< Method name, e.g. "<init>", "main", "toString"
    std::string proto;            ///< Full prototype descriptor, e.g. "(I)V"

    bool operator==(const MethodId &) const = default;
};

/** Hash functor for MethodId; suitable for std::unordered_map / std::unordered_set. */
struct MethodIdHash
{
    std::size_t operator()(const MethodId &m) const noexcept;
};

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
    /** Class-Hierarchy Analysis fan-out: a possible dispatch target reachable
     *  from a subtype of the declared class — either a subtype's own override
     *  or the definition a non-overriding subtype inherits.  Zero or more per
     *  virtual/interface call site. */
    ChaOverride,
    /** Upward resolution of the declared method_id: the declared class defines
     *  no matching method, and this edge points at the nearest definition a
     *  loaded ancestor (superclass chain, then superinterfaces) provides —
     *  what Dalvik resolution finds at runtime.  At most one per call site. */
    InheritedResolution,
};

/** A single call-site edge from one method to another.
 *
 *  All edges of one call site share `caller_node`, `kind`, and `code_offset`;
 *  they differ in `callee_node` and `origin`.  Per call site the callee nodes
 *  are pairwise distinct, and ChaOverride / InheritedResolution edges always
 *  target resolved nodes. */
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
 *  uniquely traceable via its `code_offset`, plus zero or more ChaOverride /
 *  InheritedResolution edges sharing that offset and kind (see below).
 *
 *  Virtual dispatch uses Class-Hierarchy Analysis: an invoke-virtual /
 *  invoke-interface call site records an edge to the *declared* target plus one
 *  ChaOverride edge per distinct dispatch target among the declared class's
 *  loaded descendants — a descendant's own override, or, for descendants that
 *  don't override, the definition they inherit (which may live outside the
 *  declared class's subtree, e.g. `class C extends Base implements I` where
 *  `Base` provides `I`'s method).  Static / private / constructor name
 *  collisions are never dispatch targets and are resolved past.
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
 *  invoke-super is never fanned out.  invoke-polymorphic records the declared
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
    std::unordered_map<MethodId, uint32_t, MethodIdHash> index_;
    std::vector<std::optional<ResolvedRef>> resolved_refs_; ///< parallel to nodes_
    std::weak_ptr<const detail::AnalysisContextImpl> impl_;
};

/** Build the whole-program call graph from an analysis-context implementation.
 *  Used internally by AnalysisContext::call_graph()'s lazy cache. */
CallGraph build_call_graph(std::shared_ptr<const detail::AnalysisContextImpl> impl);

} // namespace dex
