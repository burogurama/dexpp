// Upward resolution (EdgeOrigin::InheritedResolution) and the inherited-target
// CHA fan-out, over tests/data/inherit.dex (source: Inherit.java alongside).

#include <gtest/gtest.h>

#include <map>
#include <set>
#include <vector>

#include "dex.hpp"

using namespace dex;

namespace {

const CallNode *node_of(const CallGraph &g, std::string_view cls, std::string_view name)
{
    for (const auto &node : g.nodes())
        if (node.id.class_descriptor == cls && node.id.name == name)
            return &node;
    return nullptr;
}

// All edges leaving `caller` whose callee is named `name`.
std::vector<const CallEdge *> edges_to_name(const CallGraph &g, const CallNode *caller,
                                            std::string_view name)
{
    std::vector<const CallEdge *> out;
    for (uint32_t e_idx : caller->outgoing) {
        const auto &e = g.edges()[e_idx];
        if (g.nodes()[e.callee_node].id.name == name)
            out.push_back(&e);
    }
    return out;
}

const CallEdge *edge_with_origin(const std::vector<const CallEdge *> &edges, EdgeOrigin origin)
{
    for (const auto *e : edges)
        if (e->origin == origin)
            return e;
    return nullptr;
}

} // namespace

class InheritedResolutionTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        auto c = AnalysisContext::from_dex("tests/data/inherit.dex");
        ASSERT_TRUE(c.has_value()) << c.error().message;
        ctx.emplace(std::move(*c));
    }

    std::optional<AnalysisContext> ctx;
};

TEST_F(InheritedResolutionTest, VirtualResolvesToNearestAncestorNotBeyond)
{
    // UseA.callLeafM invokes m() on a Leaf receiver.  Leaf defines nothing;
    // Mid overrides Base.m — nearest-first must pick Mid.m, and the shadowed
    // Base.m must NOT get an edge.  Other.m (sibling branch) must not either.
    const auto &g = ctx->call_graph();
    const CallNode *caller = node_of(g, "LUseA;", "callLeafM");
    ASSERT_NE(caller, nullptr);

    auto edges = edges_to_name(g, caller, "m");
    const CallEdge *declared = edge_with_origin(edges, EdgeOrigin::Declared);
    ASSERT_NE(declared, nullptr);
    EXPECT_EQ(g.nodes()[declared->callee_node].id.class_descriptor, "LLeaf;");
    EXPECT_FALSE(g.nodes()[declared->callee_node].resolved);

    const CallEdge *inherited = edge_with_origin(edges, EdgeOrigin::InheritedResolution);
    ASSERT_NE(inherited, nullptr) << "call through non-defining subclass must resolve upward";
    EXPECT_EQ(g.nodes()[inherited->callee_node].id.class_descriptor, "LMid;");
    EXPECT_TRUE(g.nodes()[inherited->callee_node].resolved);
    EXPECT_EQ(inherited->kind, InvokeKind::Virtual);
    EXPECT_EQ(inherited->code_offset, declared->code_offset);

    for (const auto *e : edges) {
        EXPECT_NE(g.nodes()[e->callee_node].id.class_descriptor, "LBase;")
            << "Mid.m shadows Base.m — resolution must stop at the nearest definition";
        EXPECT_NE(g.nodes()[e->callee_node].id.class_descriptor, "LOther;")
            << "sibling-branch override must not be fanned out from a Leaf receiver";
    }
}

TEST_F(InheritedResolutionTest, InheritedEdgeAppearsInIncoming)
{
    // The motivating bug: Mid.m's incoming must contain the callLeafM site.
    const auto &g = ctx->call_graph();
    const CallNode *mid_m = node_of(g, "LMid;", "m");
    ASSERT_NE(mid_m, nullptr);

    bool from_call_leaf = false;
    for (uint32_t e_idx : mid_m->incoming) {
        const auto &e = g.edges()[e_idx];
        const auto &caller = g.nodes()[e.caller_node];
        if (caller.id.class_descriptor == "LUseA;" && caller.id.name == "callLeafM") {
            from_call_leaf = true;
            EXPECT_EQ(e.origin, EdgeOrigin::InheritedResolution);
        }
    }
    EXPECT_TRUE(from_call_leaf);
}

TEST_F(InheritedResolutionTest, StaticResolvesThroughChainOnly)
{
    // UseA.callLeafS invokes Leaf.s(); s is defined static on Base.
    const auto &g = ctx->call_graph();
    const CallNode *caller = node_of(g, "LUseA;", "callLeafS");
    ASSERT_NE(caller, nullptr);

    auto edges = edges_to_name(g, caller, "s");
    const CallEdge *inherited = edge_with_origin(edges, EdgeOrigin::InheritedResolution);
    ASSERT_NE(inherited, nullptr);
    EXPECT_EQ(g.nodes()[inherited->callee_node].id.class_descriptor, "LBase;");
    EXPECT_EQ(inherited->kind, InvokeKind::Static);
}

TEST_F(InheritedResolutionTest, WalkSkipsUnresolvedIntermediateNodes)
{
    // UseB.viaMid references Mid2.p, creating an UNRESOLVED Mid2.p node.  The
    // walk from Leaf2 (UseB.viaLeaf) passes Mid2 and must skip that node —
    // stopping there would recreate the dangling-edge bug.  Both sites resolve
    // to Base2.p.
    const auto &g = ctx->call_graph();
    const CallNode *mid2_p = node_of(g, "LMid2;", "p");
    ASSERT_NE(mid2_p, nullptr) << "viaMid must have created the Mid2.p reference node";
    EXPECT_FALSE(mid2_p->resolved);

    for (const char *caller_name : {"viaMid", "viaLeaf"}) {
        const CallNode *caller = node_of(g, "LUseB;", caller_name);
        ASSERT_NE(caller, nullptr);
        auto edges = edges_to_name(g, caller, "p");
        const CallEdge *inherited = edge_with_origin(edges, EdgeOrigin::InheritedResolution);
        ASSERT_NE(inherited, nullptr) << caller_name;
        EXPECT_EQ(g.nodes()[inherited->callee_node].id.class_descriptor, "LBase2;")
            << caller_name << " must resolve past the unresolved Mid2.p node";
    }
}

TEST_F(InheritedResolutionTest, SuperInvokeThroughNonDefiningSuper)
{
    // Leaf3.q calls super.q(); the method_id names Mid3, which inherits q from
    // Base3.
    const auto &g = ctx->call_graph();
    const CallNode *caller = node_of(g, "LLeaf3;", "q");
    ASSERT_NE(caller, nullptr);

    auto edges = edges_to_name(g, caller, "q");
    const CallEdge *declared = edge_with_origin(edges, EdgeOrigin::Declared);
    ASSERT_NE(declared, nullptr);
    EXPECT_EQ(g.nodes()[declared->callee_node].id.class_descriptor, "LMid3;");
    EXPECT_EQ(declared->kind, InvokeKind::Super);

    const CallEdge *inherited = edge_with_origin(edges, EdgeOrigin::InheritedResolution);
    ASSERT_NE(inherited, nullptr);
    EXPECT_EQ(g.nodes()[inherited->callee_node].id.class_descriptor, "LBase3;");
    EXPECT_EQ(inherited->kind, InvokeKind::Super);
}

TEST_F(InheritedResolutionTest, DefaultMethodViaChainClassInterface)
{
    // UseC.callGreet invokes greet() on SubHost.  Neither SubHost nor Host
    // defines it; the default lives on Greeter, implemented by Host — the
    // interface phase must search interfaces of every chain class, not just
    // the receiver's own (SubHost implements nothing directly).
    const auto &g = ctx->call_graph();
    const CallNode *caller = node_of(g, "LUseC;", "callGreet");
    ASSERT_NE(caller, nullptr);

    auto edges = edges_to_name(g, caller, "greet");
    const CallEdge *inherited = edge_with_origin(edges, EdgeOrigin::InheritedResolution);
    ASSERT_NE(inherited, nullptr);
    EXPECT_EQ(g.nodes()[inherited->callee_node].id.class_descriptor, "LGreeter;");

    auto m = g.method_for(g.nodes()[inherited->callee_node]);
    ASSERT_TRUE(m.has_value());
    EXPECT_TRUE(m->has_code()) << "default method has a body";
}

TEST_F(InheritedResolutionTest, DefaultPreferredOverAbstractDeclaration)
{
    // Person implements NamedDefault (default name()) which extends Named
    // (abstract name()).  The default must win over the abstract declaration.
    const auto &g = ctx->call_graph();
    const CallNode *caller = node_of(g, "LUseD;", "callPerson");
    ASSERT_NE(caller, nullptr);

    auto edges = edges_to_name(g, caller, "name");
    const CallEdge *inherited = edge_with_origin(edges, EdgeOrigin::InheritedResolution);
    ASSERT_NE(inherited, nullptr);
    EXPECT_EQ(g.nodes()[inherited->callee_node].id.class_descriptor, "LNamedDefault;");
    for (const auto *e : edges)
        EXPECT_NE(g.nodes()[e->callee_node].id.class_descriptor, "LNamed;")
            << "abstract declaration must not receive an edge when a default exists";
}

TEST_F(InheritedResolutionTest, AbstractInterfaceDeclarationFallback)
{
    // UseD.callName invokes name() through Tagged, which inherits only the
    // abstract declaration from Named — the fallback target.
    const auto &g = ctx->call_graph();
    const CallNode *caller = node_of(g, "LUseD;", "callName");
    ASSERT_NE(caller, nullptr);

    auto edges = edges_to_name(g, caller, "name");
    const CallEdge *declared = edge_with_origin(edges, EdgeOrigin::Declared);
    ASSERT_NE(declared, nullptr);
    EXPECT_EQ(g.nodes()[declared->callee_node].id.class_descriptor, "LTagged;");
    EXPECT_FALSE(g.nodes()[declared->callee_node].resolved);

    const CallEdge *inherited = edge_with_origin(edges, EdgeOrigin::InheritedResolution);
    ASSERT_NE(inherited, nullptr);
    EXPECT_EQ(g.nodes()[inherited->callee_node].id.class_descriptor, "LNamed;");
    EXPECT_EQ(inherited->kind, InvokeKind::Interface);

    auto m = g.method_for(g.nodes()[inherited->callee_node]);
    ASSERT_TRUE(m.has_value());
    EXPECT_FALSE(m->has_code());
}

TEST_F(InheritedResolutionTest, ChaFansOutToInheritedImplementationSideways)
{
    // UseE.callRun invokes run() through Runner.  FastRunner implements Runner
    // but inherits run() from RunnerBase — outside Runner's subtree.  The CHA
    // fan-out must surface RunnerBase.run as a dispatch target.
    const auto &g = ctx->call_graph();
    const CallNode *caller = node_of(g, "LUseE;", "callRun");
    ASSERT_NE(caller, nullptr);

    auto edges = edges_to_name(g, caller, "run");
    const CallEdge *declared = edge_with_origin(edges, EdgeOrigin::Declared);
    ASSERT_NE(declared, nullptr);
    EXPECT_EQ(g.nodes()[declared->callee_node].id.class_descriptor, "LRunner;");
    EXPECT_TRUE(g.nodes()[declared->callee_node].resolved) << "Runner declares run() abstract";

    const CallEdge *cha = edge_with_origin(edges, EdgeOrigin::ChaOverride);
    ASSERT_NE(cha, nullptr)
        << "non-overriding implementer's inherited definition must be a CHA target";
    EXPECT_EQ(g.nodes()[cha->callee_node].id.class_descriptor, "LRunnerBase;");

    EXPECT_EQ(edge_with_origin(edges, EdgeOrigin::InheritedResolution), nullptr)
        << "declared node is resolved — no upward resolution at this site";
}

TEST_F(InheritedResolutionTest, FrameworkBoundaryStaysUnresolved)
{
    // UseE.callUnknown invokes ArrayList.size(); nothing of ArrayList is
    // loaded, so there is nothing to resolve to.
    const auto &g = ctx->call_graph();
    const CallNode *caller = node_of(g, "LUseE;", "callUnknown");
    ASSERT_NE(caller, nullptr);

    auto edges = edges_to_name(g, caller, "size");
    ASSERT_EQ(edges.size(), 1u);
    EXPECT_EQ(edges[0]->origin, EdgeOrigin::Declared);
    EXPECT_FALSE(g.nodes()[edges[0]->callee_node].resolved);
}

TEST_F(InheritedResolutionTest, EdgeInvariantsSweep)
{
    // Per call site (caller, code_offset): exactly one Declared edge, at most
    // one InheritedResolution edge, pairwise-distinct callees, a single kind;
    // non-Declared edges always target resolved nodes.
    const auto &g = ctx->call_graph();

    struct Site
    {
        int declared = 0;
        int inherited = 0;
        std::set<uint32_t> callees;
        std::set<InvokeKind> kinds;
    };
    std::map<std::pair<uint32_t, uint32_t>, Site> sites;

    for (const auto &e : g.edges()) {
        auto &s = sites[{e.caller_node, e.code_offset}];
        if (e.origin == EdgeOrigin::Declared)
            ++s.declared;
        if (e.origin == EdgeOrigin::InheritedResolution)
            ++s.inherited;
        if (e.origin != EdgeOrigin::Declared)
            EXPECT_TRUE(g.nodes()[e.callee_node].resolved)
                << "non-declared edge to unresolved node " << e.callee_node;
        EXPECT_TRUE(s.callees.insert(e.callee_node).second)
            << "duplicate callee at caller " << e.caller_node << " offset " << e.code_offset;
        s.kinds.insert(e.kind);
    }
    for (const auto &[key, s] : sites) {
        EXPECT_EQ(s.declared, 1) << "caller " << key.first << " offset " << key.second;
        EXPECT_LE(s.inherited, 1) << "caller " << key.first << " offset " << key.second;
        EXPECT_EQ(s.kinds.size(), 1u) << "caller " << key.first << " offset " << key.second;
    }
}

TEST(InheritedResolutionCrossDex, WalkCrossesDexBoundaries)
{
    // Base2/Mid2 live in inherit_a.dex; Leaf2/UseB in inherit_b.dex.  The
    // walk goes through the whole-program index and hierarchy.
    auto ctx =
        AnalysisContext::from_dex_files({"tests/data/inherit_a.dex", "tests/data/inherit_b.dex"});
    ASSERT_TRUE(ctx.has_value()) << ctx.error().message;

    const auto &g = ctx->call_graph();
    const CallNode *caller = node_of(g, "LUseB;", "viaLeaf");
    ASSERT_NE(caller, nullptr);

    auto edges = edges_to_name(g, caller, "p");
    const CallEdge *inherited = edge_with_origin(edges, EdgeOrigin::InheritedResolution);
    ASSERT_NE(inherited, nullptr);
    EXPECT_EQ(g.nodes()[inherited->callee_node].id.class_descriptor, "LBase2;");
    EXPECT_TRUE(g.nodes()[inherited->callee_node].resolved);
}
