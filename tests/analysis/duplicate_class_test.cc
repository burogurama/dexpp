// A class defined in more than one loaded DEX file: the first definition wins
// everywhere, as on Android (an app's class loader searches its DEX files in
// order and stops at the first one that defines the class).  find_class, the
// class hierarchy, the call graph and the cross-references must all pick that
// definition, and nothing may come from the later one.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "dex.hpp"
#include "model/model.hpp"

using namespace dex;
namespace model = dex::model;

namespace {

model::MethodDef virtual_method(std::string name, model::Reg registers,
                                std::vector<model::Item> items)
{
    model::MethodDef m;
    m.name = std::move(name);
    m.proto = "()V";
    m.access = AccessFlags::Public;
    model::Code &code = m.code.emplace();
    code.registers = registers;
    code.items = std::move(items);
    return m;
}

model::ClassDef plain_class(std::string type, std::string superclass)
{
    model::ClassDef c;
    c.type = std::move(type);
    c.access = AccessFlags::Public;
    c.superclass = std::move(superclass);
    return c;
}

model::Insn call_static(std::string name)
{
    return model::Insn{model::Invoke{
        model::Invoke::Kind::Static, {}, MethodId{"LTarget;", std::move(name), "()V"}}};
}

/** The first DEX file: LDup; extends LBaseA; and implements LIfaceA;.  Its
 *  run() loads "from-first" and calls LTarget;->first().  It also defines the
 *  two base classes. */
model::Dex first_dex()
{
    model::Dex dex;
    model::ClassDef base_a = plain_class("LBaseA;", "Ljava/lang/Object;");
    base_a.methods.push_back(virtual_method("m", 1, {model::Insn{model::ReturnVoid{}}}));
    dex.classes.push_back(std::move(base_a));
    model::ClassDef base_b = plain_class("LBaseB;", "Ljava/lang/Object;");
    base_b.methods.push_back(virtual_method("m", 1, {model::Insn{model::ReturnVoid{}}}));
    dex.classes.push_back(std::move(base_b));

    model::ClassDef dup = plain_class("LDup;", "LBaseA;");
    dup.interfaces = {"LIfaceA;"};
    dup.methods.push_back(virtual_method("run", 2,
                                         {
                                             model::Insn{model::ConstString{0, "from-first"}},
                                             call_static("first"),
                                             model::Insn{model::ReturnVoid{}},
                                         }));
    dup.methods.push_back(virtual_method("m", 1, {model::Insn{model::ReturnVoid{}}}));
    dex.classes.push_back(std::move(dup));
    return dex;
}

/** The second DEX file: another LDup; that extends LBaseB;, implements
 *  LIfaceB;, has a different run() and an extra() the first one lacks.  It
 *  also defines LCaller;, which calls m() through both base classes. */
model::Dex second_dex()
{
    model::Dex dex;
    model::ClassDef dup = plain_class("LDup;", "LBaseB;");
    dup.interfaces = {"LIfaceB;"};
    dup.methods.push_back(virtual_method("run", 2,
                                         {
                                             model::Insn{model::ConstString{0, "from-second"}},
                                             call_static("second"),
                                             model::Insn{model::ReturnVoid{}},
                                         }));
    dup.methods.push_back(
        virtual_method("extra", 1, {call_static("third"), model::Insn{model::ReturnVoid{}}}));
    dup.methods.push_back(virtual_method("m", 1, {model::Insn{model::ReturnVoid{}}}));
    dex.classes.push_back(std::move(dup));

    model::ClassDef caller = plain_class("LCaller;", "Ljava/lang/Object;");
    model::MethodDef call;
    call.name = "call";
    call.proto = "(LBaseA;LBaseB;)V";
    call.access = AccessFlags::Public | AccessFlags::Static;
    model::Code &code = call.code.emplace();
    code.registers = 2;
    code.items = {
        model::Insn{
            model::Invoke{model::Invoke::Kind::Virtual, {0}, MethodId{"LBaseA;", "m", "()V"}}},
        model::Insn{
            model::Invoke{model::Invoke::Kind::Virtual, {1}, MethodId{"LBaseB;", "m", "()V"}}},
        model::Insn{model::ReturnVoid{}},
    };
    caller.methods.push_back(std::move(call));
    dex.classes.push_back(std::move(caller));
    return dex;
}

/** Write @p dex to a new temporary file and return its path. */
std::string write_temp(const model::Dex &dex, const std::string &tag)
{
    auto bytes = model::write(dex);
    EXPECT_TRUE(bytes.has_value()) << (bytes ? "" : bytes.error().message);
    if (!bytes)
        return {};
    static const unsigned run = std::random_device{}();
    auto path = std::filesystem::temp_directory_path() /
                ("dexpp_duplicate_" + std::to_string(run) + "_" + tag + ".dex");
    std::ofstream(path, std::ios::binary)
        .write(reinterpret_cast<const char *>(bytes->data()),
               static_cast<std::streamsize>(bytes->size()));
    return path.string();
}

/** The descriptor of @p cls, such as "LDup;". */
std::string_view descriptor_of(const Class &cls) { return cls.descriptor(); }

std::optional<Method> method_named(const Class &cls, std::string_view name)
{
    for (const Method &m : cls.methods())
        if (m.name() == name)
            return m;
    return std::nullopt;
}

bool contains(const std::vector<std::string_view> &list, std::string_view value)
{
    return std::find(list.begin(), list.end(), value) != list.end();
}

class DuplicateClassTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        first_path_ = write_temp(first_dex(), "first");
        second_path_ = write_temp(second_dex(), "second");
        auto ctx = AnalysisContext::from_dex_files({first_path_, second_path_});
        ASSERT_TRUE(ctx.has_value()) << ctx.error().message;
        ctx_.emplace(std::move(*ctx));

        // classes() lists both definitions, in load order.
        for (const Class &c : ctx_->classes())
            if (descriptor_of(c) == "LDup;")
                definitions_.push_back(c);
        ASSERT_EQ(definitions_.size(), 2u);
    }
    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove(first_path_, ec);
        std::filesystem::remove(second_path_, ec);
    }

    std::string first_path_, second_path_;
    std::optional<AnalysisContext> ctx_;
    std::vector<Class> definitions_; // [0] from the first file, [1] from the second
};

} // namespace

TEST_F(DuplicateClassTest, FindClassReturnsTheFirstDefinition)
{
    auto found = ctx_->find_class("LDup;");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, definitions_[0]);
    EXPECT_NE(*found, definitions_[1]);
    ASSERT_TRUE(found->superclass().has_value());
    EXPECT_EQ(found->superclass()->descriptor(), "LBaseA;");
}

TEST_F(DuplicateClassTest, ClassRefResolvesToTheFirstDefinition)
{
    // definitions_[1]'s own superclass is LBaseB;, but a ClassRef naming
    // LDup; resolves through the class index like find_class.
    auto caller = ctx_->find_class("LCaller;");
    ASSERT_TRUE(caller.has_value());
    auto run = method_named(definitions_[1], "run");
    ASSERT_TRUE(run.has_value());
    EXPECT_EQ(run->declaring_class().resolve(), definitions_[0]);
}

TEST_F(DuplicateClassTest, HierarchyUsesOnlyTheFirstDefinition)
{
    const ClassHierarchy &h = ctx_->class_hierarchy();
    EXPECT_EQ(h.superclass_of("LDup;"), "LBaseA;");
    EXPECT_EQ(h.direct_interfaces("LDup;"), std::vector<std::string_view>{"LIfaceA;"});

    EXPECT_TRUE(contains(h.subclasses("LBaseA;"), "LDup;"));
    EXPECT_FALSE(contains(h.subclasses("LBaseB;"), "LDup;"));
    EXPECT_TRUE(contains(h.implementers("LIfaceA;"), "LDup;"));
    EXPECT_TRUE(h.implementers("LIfaceB;").empty());
    EXPECT_FALSE(h.is_subtype_of("LDup;", "LBaseB;"));
    EXPECT_FALSE(h.is_subtype_of("LDup;", "LIfaceB;"));
}

TEST_F(DuplicateClassTest, CallGraphUsesOnlyTheFirstBody)
{
    const CallGraph &g = ctx_->call_graph();
    auto first_run = method_named(definitions_[0], "run");
    ASSERT_TRUE(first_run.has_value());

    const CallNode *run = g.find(MethodId{"LDup;", "run", "()V"});
    ASSERT_NE(run, nullptr);
    EXPECT_TRUE(run->resolved);
    EXPECT_EQ(g.method_for(*run), first_run);

    // One call site, from the first body only.
    ASSERT_EQ(run->outgoing.size(), 1u);
    EXPECT_EQ(g.nodes()[g.edges()[run->outgoing[0]].callee_node].id,
              (MethodId{"LTarget;", "first", "()V"}));

    // Nothing the second body calls is in the graph, and the method only the
    // second definition has is not a node.
    EXPECT_EQ(g.find(MethodId{"LTarget;", "second", "()V"}), nullptr);
    EXPECT_EQ(g.find(MethodId{"LTarget;", "third", "()V"}), nullptr);
    EXPECT_EQ(g.find(MethodId{"LDup;", "extra", "()V"}), nullptr);

    // Every resolved node's method comes from a definition the class index
    // returns.
    for (const CallNode &node : g.nodes()) {
        if (!node.resolved)
            continue;
        auto method = g.method_for(node);
        ASSERT_TRUE(method.has_value());
        if (node.id.class_descriptor == "LDup;") {
            EXPECT_EQ(method->declaring_class().resolve(), definitions_[0]) << node.id.name;
        }
    }
}

TEST_F(DuplicateClassTest, DispatchFollowsTheFirstDefinitionsSuperclass)
{
    const CallGraph &g = ctx_->call_graph();
    const CallNode *dup_m = g.find(MethodId{"LDup;", "m", "()V"});
    const CallNode *base_a_m = g.find(MethodId{"LBaseA;", "m", "()V"});
    const CallNode *base_b_m = g.find(MethodId{"LBaseB;", "m", "()V"});
    ASSERT_NE(dup_m, nullptr);
    ASSERT_NE(base_a_m, nullptr);
    ASSERT_NE(base_b_m, nullptr);

    auto targets_a = g.dispatch_targets(base_a_m->index);
    auto targets_b = g.dispatch_targets(base_b_m->index);
    EXPECT_NE(std::find(targets_a.begin(), targets_a.end(), dup_m->index), targets_a.end());
    EXPECT_EQ(std::find(targets_b.begin(), targets_b.end(), dup_m->index), targets_b.end());
}

TEST_F(DuplicateClassTest, XrefsIndexOnlyTheFirstBody)
{
    const Xrefs &x = ctx_->xrefs();
    EXPECT_TRUE(x.string_refs("from-second").empty());
    EXPECT_TRUE(x.method_refs(MethodId{"LTarget;", "second", "()V"}).empty());
    EXPECT_TRUE(x.method_refs(MethodId{"LTarget;", "third", "()V"}).empty());

    auto loads = x.string_refs("from-first");
    ASSERT_EQ(loads.size(), 1u);
    EXPECT_EQ(x.referrer_method(loads[0].referrer), method_named(definitions_[0], "run"));

    for (std::size_t r = 0; r < x.referrers().size(); ++r) {
        auto method = x.referrer_method(static_cast<uint32_t>(r));
        ASSERT_TRUE(method.has_value());
        if (method->declaring_class().descriptor() == "LDup;") {
            EXPECT_EQ(method->declaring_class().resolve(), definitions_[0]);
        }
    }
}

TEST_F(DuplicateClassTest, TheSecondDefinitionIsStillReadable)
{
    // classes() hands out the later definition too, and its own code is
    // still there to read: only the whole-program views leave it out.
    auto extra = method_named(definitions_[1], "extra");
    ASSERT_TRUE(extra.has_value());
    auto calls = extra->calls();
    ASSERT_EQ(calls.size(), 1u);
    EXPECT_EQ(calls[0].target, (MethodId{"LTarget;", "third", "()V"}));
}

TEST(DuplicateClassSameFile, LoadingAFileTwiceAddsNothing)
{
    // Every class of the second copy loses to the same class in the first, so
    // the graph, the hierarchy and the index are those of one copy.
    const std::string path = "tests/data/inherit.dex";
    auto once = AnalysisContext::from_dex(path);
    auto twice = AnalysisContext::from_dex_files({path, path});
    ASSERT_TRUE(once.has_value());
    ASSERT_TRUE(twice.has_value());

    const CallGraph &g1 = once->call_graph();
    const CallGraph &g2 = twice->call_graph();
    ASSERT_EQ(g2.nodes().size(), g1.nodes().size());
    ASSERT_EQ(g2.edges().size(), g1.edges().size());
    for (std::size_t n = 0; n < g1.nodes().size(); ++n) {
        EXPECT_EQ(g2.nodes()[n].id, g1.nodes()[n].id);
        EXPECT_EQ(g2.nodes()[n].outgoing.size(), g1.nodes()[n].outgoing.size());
        EXPECT_EQ(g2.dispatch_targets(static_cast<uint32_t>(n)).size(),
                  g1.dispatch_targets(static_cast<uint32_t>(n)).size());
        if (!g2.nodes()[n].resolved)
            continue;
        auto method = g2.method_for(g2.nodes()[n]);
        ASSERT_TRUE(method.has_value());
        auto declaring = method->declaring_class().resolve();
        ASSERT_TRUE(declaring.is_valid());
        // The class index points into the first copy; so does the method.
        EXPECT_EQ(twice->find_class(descriptor_of(declaring)), declaring);
    }

    const Xrefs &x1 = once->xrefs();
    const Xrefs &x2 = twice->xrefs();
    EXPECT_EQ(x2.referrers().size(), x1.referrers().size());
    for (std::string_view s : x1.referenced_strings())
        EXPECT_EQ(x2.string_refs(s).size(), x1.string_refs(s).size()) << s;

    const ClassHierarchy &h1 = once->class_hierarchy();
    const ClassHierarchy &h2 = twice->class_hierarchy();
    for (const Class &c : once->classes()) {
        std::string_view d = descriptor_of(c);
        EXPECT_EQ(h2.superclass_of(d), h1.superclass_of(d)) << d;
        EXPECT_EQ(h2.direct_interfaces(d), h1.direct_interfaces(d)) << d;
        EXPECT_EQ(h2.subclasses(d), h1.subclasses(d)) << d;
        EXPECT_EQ(h2.implementers(d), h1.implementers(d)) << d;
    }
}
