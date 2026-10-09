#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dex {

namespace detail {
struct AnalysisContextImpl;
} // namespace detail

/** Whole-program inheritance and interface-implementation graph.
 *
 *  Built lazily on first `AnalysisContext::class_hierarchy()` access and cached
 *  on the underlying impl.  Every class descriptor referenced as a superclass
 *  or interface is represented, even if the class is not defined in any loaded
 *  DEX file (e.g. `Ljava/lang/Object;`) — those nodes have `is_loaded() == false`
 *  and no direct supertypes recorded, but participate in subtype queries
 *  initiated from loaded descendants.
 *
 *  All query results return `std::string_view` views into the hierarchy's
 *  internal storage; valid for the lifetime of the AnalysisContext.
 *
 *  Move-only: copying would deep-copy the entire descriptor index. */
class ClassHierarchy
{
  public:
    ClassHierarchy() = default;
    ClassHierarchy(const ClassHierarchy &) = delete;
    ClassHierarchy &operator=(const ClassHierarchy &) = delete;
    ClassHierarchy(ClassHierarchy &&) = default;
    ClassHierarchy &operator=(ClassHierarchy &&) = default;

    /** Direct supertypes: at most one superclass plus any directly implemented
     *  interfaces, in that order.  Empty if `descriptor` is not in the graph. */
    std::vector<std::string_view> supertypes(std::string_view descriptor) const;

    /** The direct superclass descriptor, or nullopt when none is recorded —
     *  for Object, for descriptors not in the graph, and for unloaded
     *  placeholder nodes (the chain is unknown past a framework boundary; the
     *  three cases are not distinguished).  The view points into the
     *  hierarchy's storage, valid for the AnalysisContext's lifetime. */
    std::optional<std::string_view> superclass_of(std::string_view descriptor) const;

    /** Directly implemented (or, for an interface, directly extended)
     *  interface descriptors, in declaration order.  Empty for unknown or
     *  unloaded descriptors. */
    std::vector<std::string_view> direct_interfaces(std::string_view descriptor) const;

    /** Direct subclasses: classes whose `extends` clause names @p descriptor. */
    std::vector<std::string_view> subclasses(std::string_view descriptor) const;

    /** Direct implementers: classes whose `implements` clause names @p descriptor.
     *  Only meaningful when @p descriptor is an interface. */
    std::vector<std::string_view> implementers(std::string_view descriptor) const;

    /** Transitive supertypes (BFS).  Includes both class ancestors and all
     *  ancestor interfaces.  Does NOT include @p descriptor itself. */
    std::vector<std::string_view> all_supertypes(std::string_view descriptor) const;

    /** Transitive descendants: every class that extends @p descriptor (directly
     *  or transitively) plus every class that implements @p descriptor (if it's
     *  an interface), recursively.  Does NOT include @p descriptor itself. */
    std::vector<std::string_view> all_descendants(std::string_view descriptor) const;

    /** True iff @p super == @p sub (reflexive) or @p super appears in
     *  `all_supertypes(@p sub)`.  Returns false if @p sub is not in the graph. */
    bool is_subtype_of(std::string_view sub, std::string_view super) const;

    /** True iff a class with this descriptor is defined in some loaded DEX.
     *  Descriptor-only nodes (referenced but not loaded) return false. */
    bool is_loaded(std::string_view descriptor) const;

    /** True iff the descriptor is loaded AND has the Interface access flag.
     *  Returns false for unloaded descriptors (we cannot know without bytecode). */
    bool is_interface(std::string_view descriptor) const;

    bool empty() const { return nodes_.empty(); }

  private:
    friend ClassHierarchy build_class_hierarchy(const detail::AnalysisContextImpl &);

    struct Node
    {
        std::optional<std::string> superclass; ///< Empty for Object / unknown / unloaded.
        std::vector<std::string> interfaces;
        std::vector<std::string> subclasses;
        std::vector<std::string> implementers;
        bool loaded = false;
        bool is_iface = false;
    };

    /** Heterogeneous hasher/equal so queries take string_view without an
     *  intermediate std::string copy. */
    struct SvHash
    {
        using is_transparent = void;
        std::size_t operator()(std::string_view s) const noexcept
        {
            return std::hash<std::string_view>{}(s);
        }
        std::size_t operator()(const std::string &s) const noexcept
        {
            return std::hash<std::string_view>{}(s);
        }
    };
    struct SvEq
    {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
    };

    const Node *find(std::string_view descriptor) const;

    std::unordered_map<std::string, Node, SvHash, SvEq> nodes_;
};

/** Build the whole-program class hierarchy from an analysis-context impl.
 *  Used internally by AnalysisContext::class_hierarchy()'s lazy cache. */
ClassHierarchy build_class_hierarchy(const detail::AnalysisContextImpl &impl);

} // namespace dex
