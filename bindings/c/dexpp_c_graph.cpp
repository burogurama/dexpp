/* C ABI over the dexpp C++ API: the call graph, the class hierarchy and the
 * cross-reference index.  Compiled as C++23. */

#include "dexpp.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dexpp_c_internal.hpp"

using namespace dexpp_c;

namespace {

DexppCallNode call_node(const dex::CallNode &n)
{
    return DexppCallNode{n.index, method_id(n.id), n.resolved ? 1 : 0};
}

DexppCallEdge call_edge(const dex::CallEdge &e)
{
    return DexppCallEdge{e.caller_node, e.callee_node, invoke_kind(e.kind), e.code_offset,
                         edge_origin(e.origin)};
}

dex::MethodId method_id_of(const char *class_descriptor, const char *name, const char *proto)
{
    return dex::MethodId{class_descriptor, name, proto};
}

dex::FieldId field_id_of(const char *class_descriptor, const char *name, const char *type)
{
    return dex::FieldId{class_descriptor, name, type};
}

template <typename Range> DexppFieldXref *field_xrefs(const Range &sites, size_t *count)
{
    std::size_t n = 0;
    for ([[maybe_unused]] const auto &s : sites)
        ++n;
    *count = n;
    DexppFieldXref *out = alloc_array<DexppFieldXref>(n);
    std::size_t i = 0;
    for (const auto &s : sites)
        out[i++] =
            DexppFieldXref{s.referrer, s.code_offset, s.is_write ? 1 : 0, s.is_static ? 1 : 0};
    return out;
}

DexppMethodXref *method_xrefs(std::span<const dex::MethodXref> sites, size_t *count)
{
    *count = sites.size();
    DexppMethodXref *out = alloc_array<DexppMethodXref>(sites.size());
    for (std::size_t i = 0; i < sites.size(); ++i)
        out[i] =
            DexppMethodXref{sites[i].referrer, sites[i].code_offset, invoke_kind(sites[i].kind)};
    return out;
}

} // namespace

extern "C" {

/* ---- Call graph --------------------------------------------------------- */

size_t dexpp_call_graph_node_count(const DexppCallGraph *g) { return g->graph->nodes().size(); }

size_t dexpp_call_graph_edge_count(const DexppCallGraph *g) { return g->graph->edges().size(); }

DexppCallNode *dexpp_call_graph_nodes(const DexppCallGraph *g, size_t *count)
{
    std::span<const dex::CallNode> nodes = g->graph->nodes();
    *count = nodes.size();
    DexppCallNode *out = alloc_array<DexppCallNode>(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i)
        out[i] = call_node(nodes[i]);
    return out;
}

int dexpp_call_graph_node(const DexppCallGraph *g, uint32_t node, DexppCallNode *out)
{
    std::span<const dex::CallNode> nodes = g->graph->nodes();
    if (node >= nodes.size())
        return 0;
    *out = call_node(nodes[node]);
    return 1;
}

DexppCallEdge *dexpp_call_graph_edges(const DexppCallGraph *g, size_t *count)
{
    std::span<const dex::CallEdge> edges = g->graph->edges();
    *count = edges.size();
    DexppCallEdge *out = alloc_array<DexppCallEdge>(edges.size());
    for (size_t i = 0; i < edges.size(); ++i)
        out[i] = call_edge(edges[i]);
    return out;
}

int dexpp_call_graph_edge(const DexppCallGraph *g, uint32_t edge, DexppCallEdge *out)
{
    std::span<const dex::CallEdge> edges = g->graph->edges();
    if (edge >= edges.size())
        return 0;
    *out = call_edge(edges[edge]);
    return 1;
}

int64_t dexpp_call_graph_find(const DexppCallGraph *g, const char *class_descriptor,
                              const char *name, const char *proto)
{
    const dex::CallNode *node = g->graph->find(method_id_of(class_descriptor, name, proto));
    if (node == nullptr)
        return -1;
    return static_cast<int64_t>(node->index);
}

uint32_t *dexpp_call_graph_out_edges(const DexppCallGraph *g, uint32_t node, size_t *count)
{
    std::span<const dex::CallNode> nodes = g->graph->nodes();
    if (node >= nodes.size()) {
        *count = 0;
        return nullptr;
    }
    return index_array(nodes[node].outgoing, count);
}

uint32_t *dexpp_call_graph_in_edges(const DexppCallGraph *g, uint32_t node, size_t *count)
{
    std::span<const dex::CallNode> nodes = g->graph->nodes();
    if (node >= nodes.size()) {
        *count = 0;
        return nullptr;
    }
    return index_array(nodes[node].incoming, count);
}

uint32_t *dexpp_call_graph_dispatch_targets(const DexppCallGraph *g, uint32_t node, size_t *count)
{
    return index_array(g->graph->dispatch_targets(node), count);
}

uint32_t *dexpp_call_graph_dispatch_sources(const DexppCallGraph *g, uint32_t node, size_t *count)
{
    return index_array(g->graph->dispatch_sources(node), count);
}

uint32_t *dexpp_call_graph_callees(const DexppCallGraph *g, uint32_t node, size_t *count)
{
    return index_array(g->graph->callees(node), count);
}

uint32_t *dexpp_call_graph_callers(const DexppCallGraph *g, uint32_t node, size_t *count)
{
    return index_array(g->graph->callers(node), count);
}

uint32_t *dexpp_call_graph_edge_dispatch_targets(const DexppCallGraph *g, uint32_t edge,
                                                 size_t *count)
{
    std::span<const dex::CallEdge> edges = g->graph->edges();
    if (edge >= edges.size()) {
        *count = 0;
        return nullptr;
    }
    return index_array(g->graph->dispatch_targets(edges[edge]), count);
}

uint32_t *dexpp_call_graph_edges_where(const DexppCallGraph *g, int origin, int kind, size_t *count)
{
    std::span<const dex::CallEdge> edges = g->graph->edges();
    std::vector<uint32_t> matches;
    for (size_t i = 0; i < edges.size(); ++i) {
        if (origin >= 0 && static_cast<int>(edge_origin(edges[i].origin)) != origin)
            continue;
        if (kind >= 0 && static_cast<int>(invoke_kind(edges[i].kind)) != kind)
            continue;
        matches.push_back(static_cast<uint32_t>(i));
    }
    return index_array(matches, count);
}

DexppMethod *dexpp_call_graph_method_for(const DexppCallGraph *g, uint32_t node)
{
    std::span<const dex::CallNode> nodes = g->graph->nodes();
    if (node >= nodes.size())
        return nullptr;
    std::optional<dex::Method> m = g->graph->method_for(nodes[node]);
    if (!m)
        return nullptr;
    return new DexppMethod{std::move(*m)};
}

void dexpp_call_graph_free(DexppCallGraph *g) { delete g; }

void dexpp_call_node_clear(DexppCallNode *node) { clear(node->id); }

void dexpp_call_node_array_free(DexppCallNode *nodes, size_t count)
{
    if (nodes == nullptr)
        return;
    for (size_t i = 0; i < count; ++i)
        clear(nodes[i].id);
    std::free(nodes);
}

void dexpp_call_edge_array_free(DexppCallEdge *edges, size_t /*count*/) { std::free(edges); }

/* ---- Class hierarchy ---------------------------------------------------- */

char **dexpp_class_hierarchy_supertypes(const DexppClassHierarchy *h, const char *descriptor,
                                        size_t *count)
{
    return string_array(h->hierarchy->supertypes(descriptor), count);
}

char *dexpp_class_hierarchy_superclass_of(const DexppClassHierarchy *h, const char *descriptor)
{
    std::optional<std::string_view> sup = h->hierarchy->superclass_of(descriptor);
    if (!sup)
        return nullptr;
    return cstr(*sup);
}

char **dexpp_class_hierarchy_direct_interfaces(const DexppClassHierarchy *h, const char *descriptor,
                                               size_t *count)
{
    return string_array(h->hierarchy->direct_interfaces(descriptor), count);
}

char **dexpp_class_hierarchy_subclasses(const DexppClassHierarchy *h, const char *descriptor,
                                        size_t *count)
{
    return string_array(h->hierarchy->subclasses(descriptor), count);
}

char **dexpp_class_hierarchy_implementers(const DexppClassHierarchy *h, const char *descriptor,
                                          size_t *count)
{
    return string_array(h->hierarchy->implementers(descriptor), count);
}

char **dexpp_class_hierarchy_all_supertypes(const DexppClassHierarchy *h, const char *descriptor,
                                            size_t *count)
{
    return string_array(h->hierarchy->all_supertypes(descriptor), count);
}

char **dexpp_class_hierarchy_all_descendants(const DexppClassHierarchy *h, const char *descriptor,
                                             size_t *count)
{
    return string_array(h->hierarchy->all_descendants(descriptor), count);
}

int dexpp_class_hierarchy_is_subtype_of(const DexppClassHierarchy *h, const char *sub,
                                        const char *super)
{
    return h->hierarchy->is_subtype_of(sub, super) ? 1 : 0;
}

int dexpp_class_hierarchy_is_loaded(const DexppClassHierarchy *h, const char *descriptor)
{
    return h->hierarchy->is_loaded(descriptor) ? 1 : 0;
}

int dexpp_class_hierarchy_is_interface(const DexppClassHierarchy *h, const char *descriptor)
{
    return h->hierarchy->is_interface(descriptor) ? 1 : 0;
}

int dexpp_class_hierarchy_empty(const DexppClassHierarchy *h)
{
    return h->hierarchy->empty() ? 1 : 0;
}

void dexpp_class_hierarchy_free(DexppClassHierarchy *h) { delete h; }

/* ---- Cross-references --------------------------------------------------- */

size_t dexpp_xrefs_referrer_count(const DexppXrefs *x) { return x->xrefs->referrers().size(); }

DexppMethodId *dexpp_xrefs_referrers(const DexppXrefs *x, size_t *count)
{
    std::span<const dex::MethodId> ids = x->xrefs->referrers();
    *count = ids.size();
    DexppMethodId *out = alloc_array<DexppMethodId>(ids.size());
    for (size_t i = 0; i < ids.size(); ++i)
        out[i] = method_id(ids[i]);
    return out;
}

int dexpp_xrefs_referrer_id(const DexppXrefs *x, uint32_t referrer, DexppMethodId *out)
{
    if (referrer >= x->xrefs->referrers().size())
        return 0;
    *out = method_id(x->xrefs->referrer_id(referrer));
    return 1;
}

DexppMethod *dexpp_xrefs_referrer_method(const DexppXrefs *x, uint32_t referrer)
{
    if (referrer >= x->xrefs->referrers().size())
        return nullptr;
    std::optional<dex::Method> m = x->xrefs->referrer_method(referrer);
    if (!m)
        return nullptr;
    return new DexppMethod{std::move(*m)};
}

DexppMethodXref *dexpp_xrefs_method_refs(const DexppXrefs *x, const char *class_descriptor,
                                         const char *name, const char *proto, size_t *count)
{
    return method_xrefs(x->xrefs->method_refs(method_id_of(class_descriptor, name, proto)), count);
}

DexppMethodRefGroup *dexpp_xrefs_method_refs_by_name(const DexppXrefs *x, const char *name,
                                                     size_t *count)
{
    auto groups = x->xrefs->method_refs_by_name(name);
    *count = groups.size();
    DexppMethodRefGroup *out = alloc_array<DexppMethodRefGroup>(groups.size());
    for (size_t i = 0; i < groups.size(); ++i) {
        out[i].target = method_id(*groups[i].first);
        out[i].sites = method_xrefs(groups[i].second, &out[i].site_count);
    }
    return out;
}

DexppFieldXref *dexpp_xrefs_field_refs(const DexppXrefs *x, const char *class_descriptor,
                                       const char *name, const char *type, size_t *count)
{
    return field_xrefs(x->xrefs->field_refs(field_id_of(class_descriptor, name, type)), count);
}

DexppFieldXref *dexpp_xrefs_field_reads(const DexppXrefs *x, const char *class_descriptor,
                                        const char *name, const char *type, size_t *count)
{
    return field_xrefs(x->xrefs->field_reads(field_id_of(class_descriptor, name, type)), count);
}

DexppFieldXref *dexpp_xrefs_field_writes(const DexppXrefs *x, const char *class_descriptor,
                                         const char *name, const char *type, size_t *count)
{
    return field_xrefs(x->xrefs->field_writes(field_id_of(class_descriptor, name, type)), count);
}

DexppStringXref *dexpp_xrefs_string_refs(const DexppXrefs *x, const char *value, size_t length,
                                         size_t *count)
{
    std::span<const dex::StringXref> sites =
        x->xrefs->string_refs(std::string_view(value == nullptr ? "" : value, length));
    *count = sites.size();
    DexppStringXref *out = alloc_array<DexppStringXref>(sites.size());
    for (size_t i = 0; i < sites.size(); ++i)
        out[i] = DexppStringXref{sites[i].referrer, sites[i].code_offset};
    return out;
}

DexppText *dexpp_xrefs_referenced_strings(const DexppXrefs *x, size_t *count)
{
    std::vector<std::string_view> strings = x->xrefs->referenced_strings();
    *count = strings.size();
    DexppText *out = alloc_array<DexppText>(strings.size());
    for (size_t i = 0; i < strings.size(); ++i)
        out[i] = text(strings[i]);
    return out;
}

DexppTypeXref *dexpp_xrefs_type_refs(const DexppXrefs *x, const char *descriptor, size_t *count)
{
    std::span<const dex::TypeXref> sites = x->xrefs->type_refs(descriptor);
    *count = sites.size();
    DexppTypeXref *out = alloc_array<DexppTypeXref>(sites.size());
    for (size_t i = 0; i < sites.size(); ++i)
        out[i] =
            DexppTypeXref{sites[i].referrer, sites[i].code_offset, type_ref_kind(sites[i].kind)};
    return out;
}

int dexpp_xrefs_empty(const DexppXrefs *x) { return x->xrefs->empty() ? 1 : 0; }

void dexpp_xrefs_free(DexppXrefs *x) { delete x; }

void dexpp_method_xref_array_free(DexppMethodXref *xrefs, size_t /*count*/) { std::free(xrefs); }

void dexpp_method_ref_group_array_free(DexppMethodRefGroup *groups, size_t count)
{
    if (groups == nullptr)
        return;
    for (size_t i = 0; i < count; ++i) {
        clear(groups[i].target);
        std::free(groups[i].sites);
    }
    std::free(groups);
}

void dexpp_field_xref_array_free(DexppFieldXref *xrefs, size_t /*count*/) { std::free(xrefs); }

void dexpp_string_xref_array_free(DexppStringXref *xrefs, size_t /*count*/) { std::free(xrefs); }

void dexpp_type_xref_array_free(DexppTypeXref *xrefs, size_t /*count*/) { std::free(xrefs); }

} // extern "C"
