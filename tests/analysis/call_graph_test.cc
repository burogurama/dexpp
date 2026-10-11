#include <gtest/gtest.h>

#include <algorithm>
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

TEST(ChaTest, InterfaceCallFansOutToOverrides)
{
    auto ctx = AnalysisContext::from_dex("tests/data/polymorphic.dex");
    ASSERT_TRUE(ctx.has_value()) << ctx.error().message;
    const auto &g = ctx->call_graph();

    const CallNode *noise = find_node_by_class_and_name(g, "LKennel;", "noise");
    ASSERT_NE(noise, nullptr);

    const CallEdge *declared = nullptr;
    const CallEdge *to_dog = nullptr;
    const CallEdge *to_cat = nullptr;
    for (uint32_t e_idx : noise->outgoing) {
        const auto &e = g.edges()[e_idx];
        const auto &callee = g.nodes()[e.callee_node];
        if (callee.id.name != "sound")
            continue;
        if (callee.id.class_descriptor == "LAnimal;")
            declared = &e;
        else if (callee.id.class_descriptor == "LDog;")
            to_dog = &e;
        else if (callee.id.class_descriptor == "LCat;")
            to_cat = &e;
    }

    ASSERT_NE(declared, nullptr) << "declared-target edge to Animal.sound must exist";
    EXPECT_EQ(declared->origin, EdgeOrigin::Declared);
    EXPECT_EQ(declared->kind, InvokeKind::Interface);
    // Animal is loaded, so its abstract sound() registers as a resolved node —
    // but one with no code behind it.
    ASSERT_TRUE(g.nodes()[declared->callee_node].resolved);
    auto declared_method = g.method_for(g.nodes()[declared->callee_node]);
    ASSERT_TRUE(declared_method.has_value());
    EXPECT_FALSE(declared_method->has_code());

    ASSERT_NE(to_dog, nullptr) << "CHA must add an edge to the Dog.sound override";
    ASSERT_NE(to_cat, nullptr) << "CHA must add an edge to the Cat.sound override";
    for (const CallEdge *e : {to_dog, to_cat}) {
        EXPECT_EQ(e->origin, EdgeOrigin::ChaOverride);
        EXPECT_EQ(e->kind, InvokeKind::Interface);
        EXPECT_EQ(e->code_offset, declared->code_offset)
            << "override edges share the declared edge's call site";
        EXPECT_TRUE(g.nodes()[e->callee_node].resolved);
    }
}

TEST(ChaTest, VirtualCallThroughAbstractClassFansOut)
{
    auto ctx = AnalysisContext::from_dex("tests/data/polymorphic.dex");
    ASSERT_TRUE(ctx.has_value());
    const auto &g = ctx->call_graph();

    // Pet.describe() calls this.sound(); Pet itself never defines sound, so
    // the only resolved targets are the subtype overrides.
    const CallNode *describe = find_node_by_class_and_name(g, "LPet;", "describe");
    ASSERT_NE(describe, nullptr);

    bool dog_override = false;
    bool cat_override = false;
    bool inherited_to_animal = false;
    for (uint32_t e_idx : describe->outgoing) {
        const auto &e = g.edges()[e_idx];
        const auto &callee = g.nodes()[e.callee_node];
        if (callee.id.name != "sound")
            continue;
        if (e.origin == EdgeOrigin::ChaOverride && callee.id.class_descriptor == "LDog;")
            dog_override = true;
        if (e.origin == EdgeOrigin::ChaOverride && callee.id.class_descriptor == "LCat;")
            cat_override = true;
        // Pet defines no sound(); the declared method_id resolves upward to
        // the abstract interface declaration Animal.sound.
        if (e.origin == EdgeOrigin::InheritedResolution && callee.id.class_descriptor == "LAnimal;")
            inherited_to_animal = true;
    }
    EXPECT_TRUE(dog_override);
    EXPECT_TRUE(cat_override);
    EXPECT_TRUE(inherited_to_animal)
        << "Pet.sound must resolve upward to the Animal.sound declaration";
}

TEST(ChaTest, LeafVirtualCallHasNoOverrideEdges)
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
        EXPECT_EQ(e.origin, EdgeOrigin::Declared) << "Dog has no subtypes — no fan-out expected";
        EXPECT_TRUE(g.nodes()[e.callee_node].resolved);
    }
    EXPECT_EQ(sound_edges, 1);
}

TEST(ChaTest, SuperInvokeNotFannedOut)
{
    auto ctx = AnalysisContext::from_dex("tests/data/polymorphic.dex");
    ASSERT_TRUE(ctx.has_value());
    const auto &g = ctx->call_graph();

    // Dog.describe() calls super.describe().  Pet.describe is overridden by
    // Dog.describe itself — fanning out a super invoke would add a bogus
    // self-recursion edge.
    const CallNode *dog_describe = find_node_by_class_and_name(g, "LDog;", "describe");
    ASSERT_NE(dog_describe, nullptr);

    int super_edges = 0;
    for (uint32_t e_idx : dog_describe->outgoing) {
        const auto &e = g.edges()[e_idx];
        const auto &callee = g.nodes()[e.callee_node];
        if (e.kind == InvokeKind::Super) {
            ++super_edges;
            EXPECT_EQ(callee.id.class_descriptor, "LPet;");
            EXPECT_EQ(callee.id.name, "describe");
            EXPECT_EQ(e.origin, EdgeOrigin::Declared);
        }
        EXPECT_FALSE(callee.id.class_descriptor == "LDog;" && callee.id.name == "describe")
            << "super.describe() must not produce a self edge via CHA";
    }
    EXPECT_EQ(super_edges, 1);
}

TEST(ChaTest, OverrideEdgesAppearInIncoming)
{
    auto ctx = AnalysisContext::from_dex("tests/data/polymorphic.dex");
    ASSERT_TRUE(ctx.has_value());
    const auto &g = ctx->call_graph();

    const CallNode *dog_sound = find_node_by_class_and_name(g, "LDog;", "sound");
    ASSERT_NE(dog_sound, nullptr);

    bool from_noise = false;
    bool from_describe = false;
    for (uint32_t e_idx : dog_sound->incoming) {
        const auto &e = g.edges()[e_idx];
        const auto &caller = g.nodes()[e.caller_node];
        if (e.origin != EdgeOrigin::ChaOverride)
            continue;
        if (caller.id.class_descriptor == "LKennel;" && caller.id.name == "noise")
            from_noise = true;
        if (caller.id.class_descriptor == "LPet;" && caller.id.name == "describe")
            from_describe = true;
    }
    EXPECT_TRUE(from_noise);
    EXPECT_TRUE(from_describe);
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
