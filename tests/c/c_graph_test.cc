// The C API for whole-program analysis (bindings/c/dexpp.h): the call graph,
// the class hierarchy and cross-references.  See c_test_util.hpp.

#include <gtest/gtest.h>

#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include "c_test_util.hpp"
#include "dexpp.h"

using c_test::message;
using c_test::open_dex;
using c_test::str;
using c_test::take;
using c_test::take_all;

namespace {

/** "Lclass;->name" of a node. */
std::string label(const DexppCallGraph *g, uint32_t node)
{
    DexppCallNode n{};
    EXPECT_EQ(dexpp_call_graph_node(g, node, &n), 1);
    std::string out = std::string(n.id.class_descriptor) + "->" + n.id.name;
    dexpp_call_node_clear(&n);
    return out;
}

std::set<std::string> labels(const DexppCallGraph *g, uint32_t *nodes, size_t count)
{
    std::set<std::string> out;
    for (size_t i = 0; i < count; ++i)
        out.insert(label(g, nodes[i]));
    dexpp_index_array_free(nodes, count);
    return out;
}

DexppCallGraph *graph_of(const DexppContext *ctx)
{
    DexppError *err = nullptr;
    DexppCallGraph *g = dexpp_context_call_graph(ctx, &err);
    EXPECT_NE(g, nullptr) << message(err);
    EXPECT_EQ(err, nullptr);
    dexpp_error_free(err);
    return g;
}

} // namespace

TEST(CApiCallGraph, NodesEdgesAndLookup)
{
    DexppContext *ctx = open_dex("tests/data/polymorphic.dex");
    ASSERT_NE(ctx, nullptr);
    DexppCallGraph *g = graph_of(ctx);
    ASSERT_NE(g, nullptr);
    dexpp_context_free(ctx); // the graph keeps the analysis alive

    size_t node_count = 0;
    DexppCallNode *nodes = dexpp_call_graph_nodes(g, &node_count);
    EXPECT_EQ(node_count, dexpp_call_graph_node_count(g));
    ASSERT_GT(node_count, 0u);
    for (size_t i = 0; i < node_count; ++i)
        EXPECT_EQ(nodes[i].index, i);
    dexpp_call_node_array_free(nodes, node_count);

    size_t edge_count = 0;
    DexppCallEdge *edges = dexpp_call_graph_edges(g, &edge_count);
    EXPECT_EQ(edge_count, dexpp_call_graph_edge_count(g));
    ASSERT_GT(edge_count, 0u);
    DexppCallEdge one{};
    ASSERT_EQ(dexpp_call_graph_edge(g, 0, &one), 1);
    EXPECT_EQ(one.caller_node, edges[0].caller_node);
    EXPECT_EQ(one.code_offset, edges[0].code_offset);
    EXPECT_EQ(dexpp_call_graph_edge(g, static_cast<uint32_t>(edge_count), &one), 0);
    dexpp_call_edge_array_free(edges, edge_count);

    int64_t sound = dexpp_call_graph_find(g, "LAnimal;", "sound", "()Ljava/lang/String;");
    ASSERT_GE(sound, 0);
    EXPECT_EQ(dexpp_call_graph_find(g, "LNo;", "where", "()V"), -1);
    DexppCallNode n{};
    ASSERT_EQ(dexpp_call_graph_node(g, static_cast<uint32_t>(sound), &n), 1);
    EXPECT_EQ(n.resolved, 1);
    EXPECT_STREQ(n.id.proto, "()Ljava/lang/String;");
    dexpp_call_node_clear(&n);
    EXPECT_EQ(dexpp_call_graph_node(g, static_cast<uint32_t>(node_count), &n), 0);

    DexppMethod *m = dexpp_call_graph_method_for(g, static_cast<uint32_t>(sound));
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(take(dexpp_method_name(m)), "sound");
    dexpp_method_free(m);
    EXPECT_EQ(dexpp_call_graph_method_for(g, static_cast<uint32_t>(node_count)), nullptr);

    dexpp_call_graph_free(g);
}

TEST(CApiCallGraph, DispatchCallersAndCallees)
{
    DexppContext *ctx = open_dex("tests/data/polymorphic.dex");
    ASSERT_NE(ctx, nullptr);
    DexppCallGraph *g = graph_of(ctx);
    ASSERT_NE(g, nullptr);

    auto sound = static_cast<uint32_t>(
        dexpp_call_graph_find(g, "LAnimal;", "sound", "()Ljava/lang/String;"));
    auto noise = static_cast<uint32_t>(
        dexpp_call_graph_find(g, "LKennel;", "noise", "(LAnimal;)Ljava/lang/String;"));
    auto dog =
        static_cast<uint32_t>(dexpp_call_graph_find(g, "LDog;", "sound", "()Ljava/lang/String;"));

    size_t count = 0;
    uint32_t *targets = dexpp_call_graph_dispatch_targets(g, sound, &count);
    EXPECT_EQ(labels(g, targets, count), (std::set<std::string>{"LDog;->sound", "LCat;->sound"}));
    uint32_t *sources = dexpp_call_graph_dispatch_sources(g, dog, &count);
    EXPECT_EQ(labels(g, sources, count),
              (std::set<std::string>{"LAnimal;->sound", "LPet;->sound"}));
    uint32_t *callees = dexpp_call_graph_callees(g, noise, &count);
    EXPECT_EQ(labels(g, callees, count),
              (std::set<std::string>{"LAnimal;->sound", "LDog;->sound", "LCat;->sound"}));
    uint32_t *callers = dexpp_call_graph_callers(g, dog, &count);
    EXPECT_EQ(labels(g, callers, count),
              (std::set<std::string>{"LKennel;->noise", "LKennel;->dogNoise", "LPet;->describe"}));

    // noise's one call site is an invoke-interface declared against sound.
    uint32_t *out = dexpp_call_graph_out_edges(g, noise, &count);
    ASSERT_EQ(count, 1u);
    uint32_t edge = out[0];
    dexpp_index_array_free(out, count);
    DexppCallEdge e{};
    ASSERT_EQ(dexpp_call_graph_edge(g, edge, &e), 1);
    EXPECT_EQ(e.callee_node, sound);
    EXPECT_EQ(e.kind, DEXPP_INVOKE_INTERFACE);
    EXPECT_EQ(e.origin, DEXPP_EDGE_DECLARED);
    uint32_t *edge_targets = dexpp_call_graph_edge_dispatch_targets(g, edge, &count);
    EXPECT_EQ(labels(g, edge_targets, count),
              (std::set<std::string>{"LDog;->sound", "LCat;->sound"}));

    uint32_t *in = dexpp_call_graph_in_edges(g, sound, &count);
    bool from_noise = false;
    for (size_t i = 0; i < count; ++i)
        from_noise = from_noise || in[i] == edge;
    EXPECT_TRUE(from_noise);
    dexpp_index_array_free(in, count);

    // Out of range: empty, not a crash.
    in = dexpp_call_graph_in_edges(g, 1u << 30, &count);
    EXPECT_EQ(in, nullptr);
    EXPECT_EQ(count, 0u);
    out = dexpp_call_graph_out_edges(g, 1u << 30, &count);
    EXPECT_EQ(out, nullptr);
    edge_targets = dexpp_call_graph_edge_dispatch_targets(g, 1u << 30, &count);
    EXPECT_EQ(edge_targets, nullptr);

    dexpp_call_graph_free(g);
    dexpp_context_free(ctx);
}

TEST(CApiCallGraph, FiltersEdgesInCpp)
{
    DexppContext *ctx = open_dex("tests/data/polymorphic.dex");
    ASSERT_NE(ctx, nullptr);
    DexppCallGraph *g = graph_of(ctx);
    ASSERT_NE(g, nullptr);

    size_t all = 0;
    uint32_t *every = dexpp_call_graph_edges_where(g, -1, -1, &all);
    EXPECT_EQ(all, dexpp_call_graph_edge_count(g));
    dexpp_index_array_free(every, all);

    size_t count = 0;
    uint32_t *inherited =
        dexpp_call_graph_edges_where(g, DEXPP_EDGE_INHERITED_RESOLUTION, -1, &count);
    ASSERT_EQ(count, 1u);
    DexppCallEdge e{};
    ASSERT_EQ(dexpp_call_graph_edge(g, inherited[0], &e), 1);
    EXPECT_EQ(label(g, e.caller_node), "LPet;->describe");
    EXPECT_EQ(label(g, e.callee_node), "LAnimal;->sound");
    dexpp_index_array_free(inherited, count);

    uint32_t *interface_calls =
        dexpp_call_graph_edges_where(g, DEXPP_EDGE_DECLARED, DEXPP_INVOKE_INTERFACE, &count);
    EXPECT_EQ(count, 1u);
    dexpp_index_array_free(interface_calls, count);

    dexpp_call_graph_free(g);
    dexpp_context_free(ctx);
}

TEST(CApiClassHierarchy, AnswersEveryQuery)
{
    DexppContext *ctx = open_dex("tests/data/shapes.dex");
    ASSERT_NE(ctx, nullptr);
    DexppError *err = nullptr;
    DexppClassHierarchy *h = dexpp_context_class_hierarchy(ctx, &err);
    ASSERT_NE(h, nullptr) << message(err);
    dexpp_context_free(ctx);

    // Each query on a descriptor, as a sorted list.
    const auto query = [&](char **(*fn)(const DexppClassHierarchy *, const char *, size_t *),
                           const char *descriptor) {
        std::vector<std::string> out =
            c_test::list([&](size_t *c) { return fn(h, descriptor, c); });
        return std::set<std::string>(out.begin(), out.end());
    };
    EXPECT_EQ(query(dexpp_class_hierarchy_supertypes, "LCircle;"),
              (std::set<std::string>{"LAbstractShape;"}));
    EXPECT_EQ(query(dexpp_class_hierarchy_all_supertypes, "LCircle;"),
              (std::set<std::string>{"LAbstractShape;", "Ljava/lang/Object;", "LShape;"}));
    EXPECT_EQ(query(dexpp_class_hierarchy_all_descendants, "LShape;"),
              (std::set<std::string>{"LAbstractShape;", "LCircle;", "LRectangle;"}));
    EXPECT_EQ(query(dexpp_class_hierarchy_implementers, "LShape;"),
              (std::set<std::string>{"LAbstractShape;"}));
    EXPECT_EQ(query(dexpp_class_hierarchy_subclasses, "LAbstractShape;"),
              (std::set<std::string>{"LCircle;", "LRectangle;"}));
    EXPECT_EQ(query(dexpp_class_hierarchy_direct_interfaces, "LAbstractShape;"),
              (std::set<std::string>{"LShape;"}));
    size_t n = 0;
    EXPECT_EQ(take(dexpp_class_hierarchy_superclass_of(h, "LCircle;")), "LAbstractShape;");
    EXPECT_EQ(dexpp_class_hierarchy_superclass_of(h, "Ljava/lang/Object;"), nullptr);

    EXPECT_EQ(dexpp_class_hierarchy_is_subtype_of(h, "LCircle;", "LShape;"), 1);
    EXPECT_EQ(dexpp_class_hierarchy_is_subtype_of(h, "LShape;", "LCircle;"), 0);
    EXPECT_EQ(dexpp_class_hierarchy_is_interface(h, "LShape;"), 1);
    EXPECT_EQ(dexpp_class_hierarchy_is_loaded(h, "Ljava/lang/Object;"), 0);
    EXPECT_EQ(dexpp_class_hierarchy_is_loaded(h, "LCircle;"), 1);
    EXPECT_EQ(dexpp_class_hierarchy_empty(h), 0);

    // Unknown descriptor: empty results.
    char **none = dexpp_class_hierarchy_subclasses(h, "Lno/such/Class;", &n);
    EXPECT_EQ(none, nullptr);
    EXPECT_EQ(n, 0u);
    dexpp_class_hierarchy_free(h);
}

TEST(CApiXrefs, AnswersEveryQuery)
{
    DexppContext *ctx = open_dex("tests/data/classes.dex");
    ASSERT_NE(ctx, nullptr);
    DexppError *err = nullptr;
    DexppXrefs *x = dexpp_context_xrefs(ctx, &err);
    ASSERT_NE(x, nullptr) << message(err);
    dexpp_context_free(ctx);
    EXPECT_EQ(dexpp_xrefs_empty(x), 0);

    size_t count = 0;
    DexppStringXref *strings =
        dexpp_xrefs_string_refs(x, "Hello Dex++!!", std::string("Hello Dex++!!").size(), &count);
    ASSERT_EQ(count, 1u);
    uint32_t referrer = strings[0].referrer;
    dexpp_string_xref_array_free(strings, count);

    DexppMethodId id{};
    ASSERT_EQ(dexpp_xrefs_referrer_id(x, referrer, &id), 1);
    EXPECT_STREQ(id.name, "helloDex");
    dexpp_method_id_clear(&id);
    EXPECT_EQ(dexpp_xrefs_referrer_id(x, 1u << 30, &id), 0);
    DexppMethod *m = dexpp_xrefs_referrer_method(x, referrer);
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(take(dexpp_method_name(m)), "helloDex");
    dexpp_method_free(m);
    EXPECT_EQ(dexpp_xrefs_referrer_method(x, 1u << 30), nullptr);

    DexppMethodId *referrers = dexpp_xrefs_referrers(x, &count);
    EXPECT_EQ(count, dexpp_xrefs_referrer_count(x));
    ASSERT_GT(count, referrer);
    EXPECT_STREQ(referrers[referrer].name, "helloDex");
    dexpp_method_id_array_free(referrers, count);

    DexppText *loaded = dexpp_xrefs_referenced_strings(x, &count);
    ASSERT_EQ(count, 1u);
    EXPECT_EQ(str(loaded[0]), "Hello Dex++!!");
    dexpp_text_array_free(loaded, count);

    DexppFieldXref *reads =
        dexpp_xrefs_field_reads(x, "Ljava/lang/System;", "out", "Ljava/io/PrintStream;", &count);
    ASSERT_EQ(count, 1u);
    EXPECT_EQ(reads[0].is_static, 1);
    dexpp_field_xref_array_free(reads, count);
    DexppFieldXref *writes =
        dexpp_xrefs_field_writes(x, "Ljava/lang/System;", "out", "Ljava/io/PrintStream;", &count);
    EXPECT_EQ(writes, nullptr);
    EXPECT_EQ(count, 0u);
    DexppFieldXref *refs =
        dexpp_xrefs_field_refs(x, "Ljava/lang/System;", "out", "Ljava/io/PrintStream;", &count);
    EXPECT_EQ(count, 1u);
    dexpp_field_xref_array_free(refs, count);

    DexppMethodRefGroup *groups = dexpp_xrefs_method_refs_by_name(x, "println", &count);
    ASSERT_EQ(count, 1u);
    EXPECT_STREQ(groups[0].target.class_descriptor, "Ljava/io/PrintStream;");
    ASSERT_EQ(groups[0].site_count, 1u);
    EXPECT_EQ(groups[0].sites[0].kind, DEXPP_INVOKE_VIRTUAL);
    dexpp_method_ref_group_array_free(groups, count);

    DexppMethodXref *calls =
        dexpp_xrefs_method_refs(x, "LTestDex;", "helloDex", "()Ljava/lang/String;", &count);
    ASSERT_EQ(count, 1u);
    EXPECT_EQ(calls[0].kind, DEXPP_INVOKE_STATIC);
    dexpp_method_xref_array_free(calls, count);
    dexpp_xrefs_free(x);

    // Type references, and a string with a NUL byte.
    ctx = open_dex("tests/data/polymorphic.dex");
    ASSERT_NE(ctx, nullptr);
    x = dexpp_context_xrefs(ctx, nullptr);
    ASSERT_NE(x, nullptr);
    DexppTypeXref *types = dexpp_xrefs_type_refs(x, "Ljava/lang/StringBuilder;", &count);
    ASSERT_GE(count, 1u);
    EXPECT_EQ(types[0].kind, DEXPP_TYPE_USE_NEW_INSTANCE);
    dexpp_type_xref_array_free(types, count);
    dexpp_xrefs_free(x);
    dexpp_context_free(ctx);

    ctx = open_dex("tests/data/multistring.dex");
    ASSERT_NE(ctx, nullptr);
    x = dexpp_context_xrefs(ctx, nullptr);
    ASSERT_NE(x, nullptr);
    const char with_nul[] = "test\0embedded";
    strings = dexpp_xrefs_string_refs(x, with_nul, sizeof(with_nul) - 1, &count);
    EXPECT_EQ(count, 1u);
    dexpp_string_xref_array_free(strings, count);
    strings = dexpp_xrefs_string_refs(x, with_nul, 4, &count); // "test" alone is not loaded
    EXPECT_EQ(count, 0u);
    dexpp_xrefs_free(x);
    dexpp_context_free(ctx);
}
