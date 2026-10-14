#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <set>
#include <string>
#include <unordered_map>

#include "dex.hpp"

using namespace dex;

namespace {

// Find a node whose MethodId matches (class_descriptor, name) — proto-agnostic.
// Useful when we don't want to assert on the full proto string from a fixture.
const CallNode *find_node_by_class_and_name(const CallGraph &g, std::string_view cls,
                                            std::string_view name)
{
    for (const auto &node : g.nodes()) {
        if (node.id.class_descriptor == cls && node.id.name == name)
            return &node;
    }
    return nullptr;
}

uint32_t node_index(const CallGraph &g, const CallNode *node)
{
    return static_cast<uint32_t>(node - g.nodes().data());
}

// "Class.name" of every listed node, for order-independent comparisons.
template <typename Nodes> std::set<std::string> names_of(const CallGraph &g, const Nodes &nodes)
{
    std::set<std::string> out;
    for (uint32_t n : nodes) {
        const MethodId &id = g.nodes()[n].id;
        out.insert(id.class_descriptor + "." + id.name);
    }
    return out;
}

} // namespace

TEST(MethodIdTest, EqualityComponentwise)
{
    MethodId a{"LFoo;", "bar", "(I)V"};
    MethodId b{"LFoo;", "bar", "(I)V"};
    EXPECT_EQ(a, b);

    EXPECT_NE((MethodId{"LBar;", "bar", "(I)V"}), a);
    EXPECT_NE((MethodId{"LFoo;", "baz", "(I)V"}), a);
    EXPECT_NE((MethodId{"LFoo;", "bar", "(J)V"}), a);
}

TEST(MethodIdTest, HashableInUnorderedMap)
{
    std::unordered_map<MethodId, int, MethodIdHash> m;
    m[{"LFoo;", "a", "()V"}] = 1;
    m[{"LFoo;", "b", "()V"}] = 2;
    m[{"LBar;", "a", "()V"}] = 3;

    EXPECT_EQ(m.size(), 3u);
    EXPECT_EQ(m.at({"LFoo;", "a", "()V"}), 1);
    EXPECT_EQ(m.at({"LFoo;", "b", "()V"}), 2);
    EXPECT_EQ(m.at({"LBar;", "a", "()V"}), 3);
}

TEST(MethodIdTest, OverloadsDoNotCollide)
{
    // Same class + name, differing proto — must be distinct keys.
    MethodId int_overload{"LTestDex;", "foo", "(I)V"};
    MethodId long_overload{"LTestDex;", "foo", "(J)V"};
    MethodId string_overload{"LTestDex;", "foo", "(Ljava/lang/String;)V"};
    MethodId object_overload{"LTestDex;", "foo", "(Ljava/lang/Object;)V"};

    EXPECT_NE(int_overload, long_overload);
    EXPECT_NE(string_overload, object_overload)
        << "Full descriptor must distinguish reference-type overloads "
        << "(shorty would collapse both to 'L')";

    std::unordered_map<MethodId, int, MethodIdHash> m;
    m[int_overload] = 1;
    m[long_overload] = 2;
    m[string_overload] = 3;
    m[object_overload] = 4;
    EXPECT_EQ(m.size(), 4u);
}

TEST(InvokeKindTest, AllOpcodesMapped)
{
    struct Row
    {
        uint8_t opcode;
        InvokeKind expected;
    };

    constexpr Row kTable[] = {
        {0x6e, InvokeKind::Virtual},     {0x74, InvokeKind::Virtual},
        {0x6f, InvokeKind::Super},       {0x75, InvokeKind::Super},
        {0x70, InvokeKind::Direct},      {0x76, InvokeKind::Direct},
        {0x71, InvokeKind::Static},      {0x77, InvokeKind::Static},
        {0x72, InvokeKind::Interface},   {0x78, InvokeKind::Interface},
        {0xfa, InvokeKind::Polymorphic}, {0xfb, InvokeKind::Polymorphic},
        {0xfc, InvokeKind::Custom},      {0xfd, InvokeKind::Custom},
    };

    for (const auto &r : kTable) {
        auto k = invoke_kind_from_opcode(r.opcode);
        ASSERT_TRUE(k.has_value()) << "opcode 0x" << std::hex << static_cast<int>(r.opcode);
        EXPECT_EQ(*k, r.expected) << "opcode 0x" << std::hex << static_cast<int>(r.opcode);
    }

    // Non-invoke opcodes return nullopt — silent fallback would be a footgun.
    EXPECT_FALSE(invoke_kind_from_opcode(0x00).has_value()); // nop
    EXPECT_FALSE(invoke_kind_from_opcode(0x0e).has_value()); // return-void
    EXPECT_FALSE(invoke_kind_from_opcode(0x28).has_value()); // goto
}

// ===== Real-fixture tests =====

TEST(CallGraphTest, ClassesDexHasMainToHelloDex)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value()) << ctx.error().message;

    const auto &g = ctx->call_graph();
    ASSERT_FALSE(g.empty());

    const CallNode *main_node = find_node_by_class_and_name(g, "LTestDex;", "main");
    ASSERT_NE(main_node, nullptr);
    EXPECT_TRUE(main_node->resolved);

    const CallNode *hello_node = find_node_by_class_and_name(g, "LTestDex;", "helloDex");
    ASSERT_NE(hello_node, nullptr);
    EXPECT_TRUE(hello_node->resolved);

    bool found_edge = false;
    uint32_t hello_idx = node_index(g, hello_node);
    for (uint32_t e_idx : main_node->outgoing) {
        const auto &e = g.edges()[e_idx];
        if (e.callee_node == hello_idx) {
            found_edge = true;
            EXPECT_EQ(e.kind, InvokeKind::Static)
                << "main->helloDex should be invoke-static (helloDex is a static method)";
        }
    }
    EXPECT_TRUE(found_edge) << "main() must have an outgoing edge to helloDex";
}

TEST(CallGraphTest, ObjectInitNodeUnresolved)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());

    const auto &g = ctx->call_graph();
    const CallNode *obj_init = g.find(MethodId{"Ljava/lang/Object;", "<init>", "()V"});
    ASSERT_NE(obj_init, nullptr) << "Object.<init> must appear as a referenced node";
    EXPECT_FALSE(obj_init->resolved);
    EXPECT_FALSE(g.method_for(*obj_init).has_value());
}

TEST(CallGraphTest, HelloDexResolvedRoundTrips)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());

    const auto &g = ctx->call_graph();
    const CallNode *hello = find_node_by_class_and_name(g, "LTestDex;", "helloDex");
    ASSERT_NE(hello, nullptr);
    ASSERT_TRUE(hello->resolved);

    auto m = g.method_for(*hello);
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->name(), "helloDex");
    EXPECT_TRUE(m->has_code());
}

TEST(CallGraphTest, SafeParseCallsIntegerParseInt)
{
    auto ctx = AnalysisContext::from_dex("tests/data/trycatch.dex");
    ASSERT_TRUE(ctx.has_value());

    const auto &g = ctx->call_graph();
    const CallNode *safeparse = find_node_by_class_and_name(g, "LTryCatch;", "safeParse");
    ASSERT_NE(safeparse, nullptr);

    bool found = false;
    for (uint32_t e_idx : safeparse->outgoing) {
        const auto &e = g.edges()[e_idx];
        const auto &callee = g.nodes()[e.callee_node];
        if (callee.id.class_descriptor == "Ljava/lang/Integer;" && callee.id.name == "parseInt") {
            found = true;
            EXPECT_EQ(e.kind, InvokeKind::Static);
            EXPECT_FALSE(callee.resolved);
        }
    }
    EXPECT_TRUE(found) << "safeParse must call Integer.parseInt as invoke-static";
}

TEST(CallGraphTest, FibonacciComputeHasNoOutgoingEdges)
{
    // fibonacci.dex's compute() is an iterative implementation — internal
    // branches but no method calls.  This pins the boundary: control-flow
    // edges (if-*, goto) must NOT be conflated with call edges.
    auto ctx = AnalysisContext::from_dex("tests/data/fibonacci.dex");
    ASSERT_TRUE(ctx.has_value());

    const auto &g = ctx->call_graph();
    ASSERT_FALSE(g.empty());

    const CallNode *compute = find_node_by_class_and_name(g, "LFibonacci;", "compute");
    ASSERT_NE(compute, nullptr);
    EXPECT_TRUE(compute->resolved);
    EXPECT_TRUE(compute->outgoing.empty())
        << "Iterative compute() should have no outgoing call edges (only if-* / goto)";
}

TEST(CallGraphTest, AbstractMethodNoOutgoingEdges)
{
    auto ctx = AnalysisContext::from_dex("tests/data/shapes.dex");
    ASSERT_TRUE(ctx.has_value());

    const auto &g = ctx->call_graph();
    auto cls = ctx->find_class("LShape;");
    ASSERT_TRUE(cls.has_value());

    bool checked = false;
    for (const auto &m : cls->methods()) {
        EXPECT_FALSE(m.has_code());
        // Build the MethodId for this abstract method.
        auto params = m.parameters();
        std::string proto = "(";
        for (const auto &p : params)
            proto.append(p.descriptor());
        proto.push_back(')');
        proto.append(m.return_type().descriptor());

        const CallNode *node =
            g.find(MethodId{std::string(cls->name()), std::string(m.name()), proto});
        ASSERT_NE(node, nullptr);
        EXPECT_TRUE(node->outgoing.empty())
            << "Abstract method " << m.name() << " must have no outgoing edges";
        checked = true;
    }
    EXPECT_TRUE(checked);
}

TEST(CallGraphTest, ReturnsSameReferenceAcrossCalls)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());
    const auto &g1 = ctx->call_graph();
    const auto &g2 = ctx->call_graph();
    EXPECT_EQ(&g1, &g2) << "AnalysisContext::call_graph() must return cached reference";
}

TEST(CallGraphTest, EdgeOutgoingMatchesIncoming)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());
    const auto &g = ctx->call_graph();

    for (uint32_t e_idx = 0; e_idx < g.edges().size(); ++e_idx) {
        const auto &e = g.edges()[e_idx];
        const auto &caller = g.nodes()[e.caller_node];
        const auto &callee = g.nodes()[e.callee_node];

        bool in_outgoing = std::find(caller.outgoing.begin(), caller.outgoing.end(), e_idx) !=
                           caller.outgoing.end();
        bool in_incoming = std::find(callee.incoming.begin(), callee.incoming.end(), e_idx) !=
                           callee.incoming.end();
        EXPECT_TRUE(in_outgoing) << "edge " << e_idx << " missing from outgoing of "
                                 << e.caller_node;
        EXPECT_TRUE(in_incoming) << "edge " << e_idx << " missing from incoming of "
                                 << e.callee_node;
    }
}

// ===== CHA dispatch tests (polymorphic.dex) =====
//
// Fixture hierarchy: interface Animal { sound() } <- abstract Pet (no sound
// definition, calls sound() in describe()) <- Dog, Cat (both override sound;
// Dog also overrides describe() and calls super.describe()).
// Kennel.noise(Animal) invokes sound() through the interface;
// Kennel.dogNoise(Dog) invokes it on the leaf class directly.

TEST(ChaTest, InterfaceCallDispatchesToOverrides)
{
    auto ctx = AnalysisContext::from_dex("tests/data/polymorphic.dex");
    ASSERT_TRUE(ctx.has_value()) << ctx.error().message;
    const auto &g = ctx->call_graph();

    const CallNode *noise = find_node_by_class_and_name(g, "LKennel;", "noise");
    ASSERT_NE(noise, nullptr);

    std::vector<const CallEdge *> sound_edges;
    for (uint32_t e_idx : noise->outgoing) {
        const auto &e = g.edges()[e_idx];
        if (g.nodes()[e.callee_node].id.name == "sound")
            sound_edges.push_back(&e);
    }
    ASSERT_EQ(sound_edges.size(), 1u) << "dispatch targets are not stored as edges";
    const CallEdge *declared = sound_edges[0];
    EXPECT_EQ(g.nodes()[declared->callee_node].id.class_descriptor, "LAnimal;");
    EXPECT_EQ(declared->origin, EdgeOrigin::Declared);
    EXPECT_EQ(declared->kind, InvokeKind::Interface);
    // Animal is loaded, so its abstract sound() registers as a resolved node —
    // but one with no code behind it.
    ASSERT_TRUE(g.nodes()[declared->callee_node].resolved);
    auto declared_method = g.method_for(g.nodes()[declared->callee_node]);
    ASSERT_TRUE(declared_method.has_value());
    EXPECT_FALSE(declared_method->has_code());

    auto targets = g.dispatch_targets(*declared);
    EXPECT_EQ(names_of(g, targets), (std::set<std::string>{"LCat;.sound", "LDog;.sound"}));
    for (uint32_t t : targets)
        EXPECT_TRUE(g.nodes()[t].resolved);
    // The site's list is the declared node's own list, not a copy.
    auto by_node = g.dispatch_targets(declared->callee_node);
    EXPECT_EQ(targets.data(), by_node.data());
    EXPECT_EQ(targets.size(), by_node.size());
}

TEST(ChaTest, VirtualCallThroughAbstractClassDispatches)
{
    auto ctx = AnalysisContext::from_dex("tests/data/polymorphic.dex");
    ASSERT_TRUE(ctx.has_value());
    const auto &g = ctx->call_graph();

    // Pet.describe() calls this.sound(); Pet itself never defines sound, so
    // the only resolved targets are the subtype overrides.
    const CallNode *describe = find_node_by_class_and_name(g, "LPet;", "describe");
    ASSERT_NE(describe, nullptr);

    const CallEdge *declared = nullptr;
    const CallEdge *inherited = nullptr;
    for (uint32_t e_idx : describe->outgoing) {
        const auto &e = g.edges()[e_idx];
        if (g.nodes()[e.callee_node].id.name != "sound")
            continue;
        if (e.origin == EdgeOrigin::Declared)
            declared = &e;
        if (e.origin == EdgeOrigin::InheritedResolution)
            inherited = &e;
    }
    ASSERT_NE(declared, nullptr);
    EXPECT_EQ(g.nodes()[declared->callee_node].id.class_descriptor, "LPet;");
    EXPECT_FALSE(g.nodes()[declared->callee_node].resolved);
    EXPECT_EQ(names_of(g, g.dispatch_targets(*declared)),
              (std::set<std::string>{"LCat;.sound", "LDog;.sound"}));

    // Pet defines no sound(); the declared method_id resolves upward to the
    // abstract interface declaration Animal.sound.
    ASSERT_NE(inherited, nullptr) << "Pet.sound must resolve upward to Animal.sound";
    EXPECT_EQ(g.nodes()[inherited->callee_node].id.class_descriptor, "LAnimal;");
    EXPECT_TRUE(g.dispatch_targets(*inherited).empty())
        << "the site's dispatch targets hang off its Declared edge only";
}

TEST(ChaTest, LeafVirtualCallHasNoDispatchTargets)
{
    auto ctx = AnalysisContext::from_dex("tests/data/polymorphic.dex");
    ASSERT_TRUE(ctx.has_value());
    const auto &g = ctx->call_graph();

    const CallNode *dog_noise = find_node_by_class_and_name(g, "LKennel;", "dogNoise");
    ASSERT_NE(dog_noise, nullptr);

    int sound_edges = 0;
    for (uint32_t e_idx : dog_noise->outgoing) {
        const auto &e = g.edges()[e_idx];
        const auto &callee = g.nodes()[e.callee_node];
        if (callee.id.name != "sound")
            continue;
        ++sound_edges;
        EXPECT_EQ(callee.id.class_descriptor, "LDog;");
        EXPECT_EQ(e.origin, EdgeOrigin::Declared);
        EXPECT_EQ(e.kind, InvokeKind::Virtual);
        EXPECT_TRUE(callee.resolved);
        EXPECT_TRUE(g.dispatch_targets(e).empty())
            << "Dog has no subtypes — nothing to dispatch to";
    }
    EXPECT_EQ(sound_edges, 1);
}

TEST(ChaTest, SuperInvokeDoesNotDispatch)
{
    auto ctx = AnalysisContext::from_dex("tests/data/polymorphic.dex");
    ASSERT_TRUE(ctx.has_value());
    const auto &g = ctx->call_graph();

    // Dog.describe() calls super.describe().  Pet.describe is overridden by
    // Dog.describe itself — dispatching a super invoke would add a bogus
    // self-recursion target.
    const CallNode *dog_describe = find_node_by_class_and_name(g, "LDog;", "describe");
    ASSERT_NE(dog_describe, nullptr);

    int super_edges = 0;
    for (uint32_t e_idx : dog_describe->outgoing) {
        const auto &e = g.edges()[e_idx];
        const auto &callee = g.nodes()[e.callee_node];
        if (e.kind != InvokeKind::Super)
            continue;
        ++super_edges;
        EXPECT_EQ(callee.id.class_descriptor, "LPet;");
        EXPECT_EQ(callee.id.name, "describe");
        EXPECT_EQ(e.origin, EdgeOrigin::Declared);
        EXPECT_TRUE(g.dispatch_targets(e).empty());
        EXPECT_TRUE(g.dispatch_targets(e.callee_node).empty())
            << "no virtual site declares Pet.describe, so it has no dispatch targets";
    }
    EXPECT_EQ(super_edges, 1);

    uint32_t self = node_index(g, dog_describe);
    auto callees = g.callees(self);
    EXPECT_EQ(std::find(callees.begin(), callees.end(), self), callees.end())
        << "super.describe() must not make Dog.describe its own callee";
}

TEST(ChaTest, DispatchSourcesAndCallers)
{
    auto ctx = AnalysisContext::from_dex("tests/data/polymorphic.dex");
    ASSERT_TRUE(ctx.has_value());
    const auto &g = ctx->call_graph();

    const CallNode *dog_sound = find_node_by_class_and_name(g, "LDog;", "sound");
    const CallNode *cat_sound = find_node_by_class_and_name(g, "LCat;", "sound");
    ASSERT_NE(dog_sound, nullptr);
    ASSERT_NE(cat_sound, nullptr);
    uint32_t dog = node_index(g, dog_sound);
    uint32_t cat = node_index(g, cat_sound);

    // Calls declared against Animal.sound (Kennel.noise) and Pet.sound
    // (Pet.describe) can both dispatch to the overrides.
    EXPECT_EQ(names_of(g, g.dispatch_sources(dog)),
              (std::set<std::string>{"LAnimal;.sound", "LPet;.sound"}));
    EXPECT_EQ(names_of(g, g.dispatch_sources(cat)),
              (std::set<std::string>{"LAnimal;.sound", "LPet;.sound"}));

    // Only the direct call is an edge; dispatch adds no incoming edges.
    ASSERT_EQ(dog_sound->incoming.size(), 1u);
    EXPECT_EQ(g.nodes()[g.edges()[dog_sound->incoming[0]].caller_node].id.name, "dogNoise");
    EXPECT_TRUE(cat_sound->incoming.empty());

    EXPECT_EQ(names_of(g, g.callers(dog)),
              (std::set<std::string>{"LKennel;.dogNoise", "LKennel;.noise", "LPet;.describe"}));
    EXPECT_EQ(names_of(g, g.callers(cat)),
              (std::set<std::string>{"LKennel;.noise", "LPet;.describe"}));
}

TEST(ChaTest, CalleesIncludeDispatchTargets)
{
    auto ctx = AnalysisContext::from_dex("tests/data/polymorphic.dex");
    ASSERT_TRUE(ctx.has_value());
    const auto &g = ctx->call_graph();

    const CallNode *noise = find_node_by_class_and_name(g, "LKennel;", "noise");
    ASSERT_NE(noise, nullptr);
    auto callees = g.callees(node_index(g, noise));
    EXPECT_TRUE(std::is_sorted(callees.begin(), callees.end()));
    EXPECT_EQ(names_of(g, callees),
              (std::set<std::string>{"LAnimal;.sound", "LCat;.sound", "LDog;.sound"}));
}

TEST(ChaTest, DispatchRowsAreSortedResolvedAndInverse)
{
    for (const char *path : {"tests/data/polymorphic.dex", "tests/data/inherit.dex",
                             "tests/data/androguard/a2dp.Vol_137.apk"}) {
        auto ctx = std::string_view(path).ends_with(".apk") ? AnalysisContext::from_apk(path)
                                                            : AnalysisContext::from_dex(path);
        ASSERT_TRUE(ctx.has_value()) << path;
        const auto &g = ctx->call_graph();

        std::size_t targets_total = 0;
        std::size_t sources_total = 0;
        for (uint32_t n = 0; n < g.nodes().size(); ++n) {
            auto targets = g.dispatch_targets(n);
            auto sources = g.dispatch_sources(n);
            targets_total += targets.size();
            sources_total += sources.size();
            EXPECT_TRUE(std::adjacent_find(targets.begin(), targets.end(),
                                           std::greater_equal<>()) == targets.end())
                << path << ": targets of node " << n << " not strictly increasing";
            EXPECT_TRUE(std::adjacent_find(sources.begin(), sources.end(),
                                           std::greater_equal<>()) == sources.end())
                << path << ": sources of node " << n << " not strictly increasing";
            for (uint32_t t : targets) {
                EXPECT_NE(t, n) << path << ": node " << n << " dispatches to itself";
                EXPECT_TRUE(g.nodes()[t].resolved) << path;
                auto back = g.dispatch_sources(t);
                EXPECT_TRUE(std::binary_search(back.begin(), back.end(), n))
                    << path << ": " << n << " -> " << t << " missing from dispatch_sources";
            }
        }
        EXPECT_EQ(targets_total, sources_total) << path;
    }
}

TEST(ChaTest, OnlyDeclaredVirtualAndInterfaceEdgesDispatch)
{
    // In a2dp, some invoke-super sites and InheritedResolution edges point at
    // a node that has dispatch targets (from a virtual call elsewhere); the
    // edge form must not apply them there.
    auto ctx = AnalysisContext::from_apk("tests/data/androguard/a2dp.Vol_137.apk");
    ASSERT_TRUE(ctx.has_value()) << ctx.error().message;
    const auto &g = ctx->call_graph();

    std::size_t dispatching = 0;
    std::size_t excluded_with_targets = 0;
    for (const auto &e : g.edges()) {
        bool applies = e.origin == EdgeOrigin::Declared &&
                       (e.kind == InvokeKind::Virtual || e.kind == InvokeKind::Interface);
        auto by_edge = g.dispatch_targets(e);
        if (applies) {
            auto by_node = g.dispatch_targets(e.callee_node);
            EXPECT_EQ(by_edge.data(), by_node.data());
            EXPECT_EQ(by_edge.size(), by_node.size());
            dispatching += !by_edge.empty();
        }
        else {
            EXPECT_TRUE(by_edge.empty());
            excluded_with_targets += !g.dispatch_targets(e.callee_node).empty();
        }
    }
    EXPECT_GT(dispatching, 0u);
    EXPECT_GT(excluded_with_targets, 0u) << "fixture no longer exercises the edge rule";
}

TEST(ChaTest, OutOfRangeNodeIsEmpty)
{
    auto ctx = AnalysisContext::from_dex("tests/data/polymorphic.dex");
    ASSERT_TRUE(ctx.has_value());
    const auto &g = ctx->call_graph();
    auto past_end = static_cast<uint32_t>(g.nodes().size());
    EXPECT_TRUE(g.dispatch_targets(past_end).empty());
    EXPECT_TRUE(g.dispatch_sources(past_end).empty());
    EXPECT_TRUE(g.callees(past_end).empty());
    EXPECT_TRUE(g.callers(past_end).empty());

    CallGraph empty;
    EXPECT_TRUE(empty.dispatch_targets(0).empty());
    EXPECT_TRUE(empty.dispatch_sources(0).empty());
    EXPECT_TRUE(empty.callees(0).empty());
    EXPECT_TRUE(empty.callers(0).empty());
}

TEST(CallGraphTest, SameGraphWithAnyThreadCount)
{
    // Part of the build runs on one thread per DEX file; nothing about the
    // result — node numbering, edge order, dispatch rows — may depend on it.
    auto describe = [](const CallGraph &g) {
        std::vector<std::string> out;
        for (const auto &n : g.nodes())
            out.push_back(n.id.class_descriptor + "->" + n.id.name + n.id.proto +
                          (n.resolved ? "" : " (unresolved)"));
        for (const auto &e : g.edges())
            out.push_back(std::to_string(e.caller_node) + " " + std::to_string(e.callee_node) +
                          " " + std::to_string(e.code_offset) + " " +
                          std::to_string(static_cast<int>(e.kind)) + " " +
                          std::to_string(static_cast<int>(e.origin)));
        for (uint32_t n = 0; n < g.nodes().size(); ++n)
            for (uint32_t t : g.dispatch_targets(n))
                out.push_back("dispatch " + std::to_string(n) + " " + std::to_string(t));
        return out;
    };
    for (const char *path :
         {"tests/data/multidex.apk", "tests/data/app.apk", "tests/data/androguard/multidex.apk"}) {
        auto one = AnalysisContext::from_apk(path, ApkLoadOptions{.threads = 1});
        auto four = AnalysisContext::from_apk(path, ApkLoadOptions{.threads = 4});
        auto automatic = AnalysisContext::from_apk(path);
        ASSERT_TRUE(one.has_value() && four.has_value() && automatic.has_value()) << path;
        auto expected = describe(one->call_graph());
        EXPECT_FALSE(one->call_graph().edges().empty()) << path;
        EXPECT_EQ(describe(four->call_graph()), expected) << path;
        EXPECT_EQ(describe(automatic->call_graph()), expected) << path;
    }
}

TEST(CallGraphTest, MultiDexBothLoaded)
{
    auto ctx =
        AnalysisContext::from_dex_files({"tests/data/classes.dex", "tests/data/fibonacci.dex"});
    ASSERT_TRUE(ctx.has_value()) << ctx.error().message;

    const auto &g = ctx->call_graph();
    ASSERT_FALSE(g.empty());

    // Both DEX's primary classes must contribute resolved nodes.
    const CallNode *test_dex_main = find_node_by_class_and_name(g, "LTestDex;", "main");
    ASSERT_NE(test_dex_main, nullptr);
    EXPECT_TRUE(test_dex_main->resolved);

    bool fib_node_seen = false;
    for (const auto &node : g.nodes()) {
        if (node.id.class_descriptor.find("Fibonacci") != std::string::npos && node.resolved) {
            fib_node_seen = true;
            break;
        }
    }
    EXPECT_TRUE(fib_node_seen) << "fibonacci.dex must contribute at least one resolved node";
}
