// Tests for the per-method forward references (Method::calls / field_accesses /
// string_loads / type_uses) and try-block decoding (Method::try_blocks).

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <string>

#include "dex.hpp"

using namespace dex;

namespace {

Method method_named(const Class &cls, std::string_view name)
{
    for (const auto &m : cls.methods())
        if (m.name() == name)
            return m;
    throw std::runtime_error("method not found: " + std::string(name));
}

} // namespace

// TestDex.main(): calls helloDex (static) and PrintStream.println (virtual),
// reads System.out, loads no strings of its own.
TEST(ForwardRefsTest, CallsResolveTargets)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value()) << ctx.error().message;
    auto cls = ctx->find_class("LTestDex;");
    ASSERT_TRUE(cls.has_value());

    auto calls = method_named(*cls, "main").calls();
    ASSERT_GE(calls.size(), 2u);

    bool calls_hello = std::any_of(calls.begin(), calls.end(), [](const MethodCall &c) {
        return c.target.class_descriptor == "LTestDex;" && c.target.name == "helloDex" &&
               c.kind == InvokeKind::Static;
    });
    bool calls_println = std::any_of(calls.begin(), calls.end(), [](const MethodCall &c) {
        return c.target.name == "println" && c.kind == InvokeKind::Virtual;
    });
    EXPECT_TRUE(calls_hello);
    EXPECT_TRUE(calls_println);

    // Code offsets must be strictly increasing in instruction order.
    for (std::size_t i = 1; i < calls.size(); ++i)
        EXPECT_LT(calls[i - 1].code_offset, calls[i].code_offset);
}

TEST(ForwardRefsTest, FieldAccessAndStringLoad)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());
    auto cls = ctx->find_class("LTestDex;");
    ASSERT_TRUE(cls.has_value());

    auto reads = method_named(*cls, "main").field_accesses();
    ASSERT_EQ(reads.size(), 1u);
    EXPECT_EQ(reads[0].field.name, "out");
    EXPECT_EQ(reads[0].field.class_descriptor, "Ljava/lang/System;");
    EXPECT_TRUE(reads[0].is_static);
    EXPECT_FALSE(reads[0].is_write);

    auto loads = method_named(*cls, "helloDex").string_loads();
    ASSERT_EQ(loads.size(), 1u);
    EXPECT_EQ(loads[0].value, "Hello Dex++!!");
}

// Pet.describe() in polymorphic.dex constructs a StringBuilder (new-instance).
TEST(ForwardRefsTest, TypeUses)
{
    auto ctx = AnalysisContext::from_dex("tests/data/polymorphic.dex");
    ASSERT_TRUE(ctx.has_value());
    auto cls = ctx->find_class("LPet;");
    ASSERT_TRUE(cls.has_value());

    auto uses = method_named(*cls, "describe").type_uses();
    bool builds_sb = std::any_of(uses.begin(), uses.end(), [](const TypeUse &u) {
        return u.descriptor == "Ljava/lang/StringBuilder;" && u.kind == TypeRefKind::NewInstance;
    });
    EXPECT_TRUE(builds_sb);
}

TEST(ForwardRefsTest, AbstractMethodHasNoForwardRefs)
{
    auto ctx = AnalysisContext::from_dex("tests/data/shapes.dex");
    ASSERT_TRUE(ctx.has_value());
    auto iface = ctx->find_class("LShape;");
    ASSERT_TRUE(iface.has_value());

    for (const auto &m : iface->methods()) {
        EXPECT_FALSE(m.has_code());
        EXPECT_TRUE(m.calls().empty());
        EXPECT_TRUE(m.field_accesses().empty());
        EXPECT_TRUE(m.string_loads().empty());
        EXPECT_TRUE(m.type_uses().empty());
        EXPECT_TRUE(m.try_blocks().empty());
    }
}

// TryCatch.divide(): try { a / b } catch (ArithmeticException) { -1 }
TEST(TryBlocksTest, TypedCatchResolved)
{
    auto ctx = AnalysisContext::from_dex("tests/data/trycatch.dex");
    ASSERT_TRUE(ctx.has_value());
    auto cls = ctx->find_class("LTryCatch;");
    ASSERT_TRUE(cls.has_value());

    auto tries = method_named(*cls, "divide").try_blocks();
    ASSERT_EQ(tries.size(), 1u);
    EXPECT_LT(tries[0].start_offset, tries[0].end_offset);

    ASSERT_EQ(tries[0].handlers.size(), 1u);
    EXPECT_EQ(tries[0].handlers[0].type_descriptor, "Ljava/lang/ArithmeticException;");
    // The handler must lie outside the guarded region.
    EXPECT_GE(tries[0].handlers[0].handler_offset, tries[0].end_offset);
}

// safeParse() has a finally, which the compiler lowers to a catch-all handler.
TEST(TryBlocksTest, FinallyProducesCatchAll)
{
    auto ctx = AnalysisContext::from_dex("tests/data/trycatch.dex");
    ASSERT_TRUE(ctx.has_value());
    auto cls = ctx->find_class("LTryCatch;");
    ASSERT_TRUE(cls.has_value());

    auto tries = method_named(*cls, "safeParse").try_blocks();
    ASSERT_FALSE(tries.empty());

    bool any_catch_all = std::any_of(tries.begin(), tries.end(), [](const TryBlock &t) {
        return t.catch_all_offset.has_value();
    });
    bool any_nfe = std::any_of(tries.begin(), tries.end(), [](const TryBlock &t) {
        return std::any_of(t.handlers.begin(), t.handlers.end(), [](const CatchHandler &h) {
            return h.type_descriptor == "Ljava/lang/NumberFormatException;";
        });
    });
    EXPECT_TRUE(any_catch_all) << "finally must lower to a catch-all handler";
    EXPECT_TRUE(any_nfe);
}
