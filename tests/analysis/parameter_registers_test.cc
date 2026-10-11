// Tests for Method::register_count / parameter_registers — the Dalvik
// parameter-to-register mapping (the implicit `this`, declaration order, and
// wide long/double register pairs). Ground truth is from the fixtures' disasm.

#include <gtest/gtest.h>

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

Class find(const AnalysisContext &ctx, std::string_view desc)
{
    auto c = ctx.find_class(desc);
    if (!c.has_value())
        throw std::runtime_error("class not found");
    return *c;
}

} // namespace

// TestDex.main(String[] args): static, one param in the last register.
TEST(ParameterRegistersTest, StaticSingleParam)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value()) << ctx.error().message;
    auto main = method_named(find(*ctx, "LTestDex;"), "main");

    EXPECT_EQ(main.register_count(), 2);
    auto params = main.parameter_registers();
    ASSERT_EQ(params.size(), 1u);
    EXPECT_FALSE(params[0].is_this);
    EXPECT_FALSE(params[0].is_wide);
    EXPECT_EQ(params[0].reg, 1);
    EXPECT_EQ(params[0].type.descriptor(), "[Ljava/lang/String;");
}

// TestDex.helloDex(): static, no parameters.
TEST(ParameterRegistersTest, StaticNoParams)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());
    auto hello = method_named(find(*ctx, "LTestDex;"), "helloDex");
    EXPECT_TRUE(hello.parameter_registers().empty());
}

// Pet.<init>(String): instance method — `this` first, then the declared param.
TEST(ParameterRegistersTest, InstanceMethodHasThisFirst)
{
    auto ctx = AnalysisContext::from_dex("tests/data/polymorphic.dex");
    ASSERT_TRUE(ctx.has_value());
    auto ctor = method_named(find(*ctx, "LPet;"), "<init>");

    auto params = ctor.parameter_registers();
    ASSERT_EQ(params.size(), 2u);
    EXPECT_TRUE(params[0].is_this);
    EXPECT_EQ(params[0].type.descriptor(), "LPet;");
    EXPECT_EQ(params[0].reg, 0);
    EXPECT_FALSE(params[1].is_this);
    EXPECT_EQ(params[1].type.descriptor(), "Ljava/lang/String;");
    EXPECT_EQ(params[1].reg, 1);
}

// Fibonacci.classify(long): static, wide parameter occupies a register pair.
TEST(ParameterRegistersTest, WideParameterOccupiesPair)
{
    auto ctx = AnalysisContext::from_dex("tests/data/fibonacci.dex");
    ASSERT_TRUE(ctx.has_value());
    auto classify = method_named(find(*ctx, "LFibonacci;"), "classify");

    EXPECT_EQ(classify.register_count(), 5);
    auto params = classify.parameter_registers();
    ASSERT_EQ(params.size(), 1u);
    EXPECT_TRUE(params[0].is_wide);
    EXPECT_EQ(params[0].type.descriptor(), "J");
    EXPECT_EQ(params[0].reg, 3); // and v4 is the high half
}

// Fibonacci.compute(int): instance — `this` at the first incoming register,
// the int param immediately after.
TEST(ParameterRegistersTest, InstanceWithPrimitiveParam)
{
    auto ctx = AnalysisContext::from_dex("tests/data/fibonacci.dex");
    ASSERT_TRUE(ctx.has_value());
    auto compute = method_named(find(*ctx, "LFibonacci;"), "compute");

    EXPECT_EQ(compute.register_count(), 11);
    auto params = compute.parameter_registers();
    ASSERT_EQ(params.size(), 2u);
    EXPECT_TRUE(params[0].is_this);
    EXPECT_EQ(params[0].reg, 9);
    EXPECT_FALSE(params[1].is_wide);
    EXPECT_EQ(params[1].type.descriptor(), "I");
    EXPECT_EQ(params[1].reg, 10);
}

// Abstract methods have no register frame.
TEST(ParameterRegistersTest, AbstractMethodHasNoRegisters)
{
    auto ctx = AnalysisContext::from_dex("tests/data/shapes.dex");
    ASSERT_TRUE(ctx.has_value());
    auto iface = find(*ctx, "LShape;");
    for (const auto &m : iface.methods()) {
        EXPECT_FALSE(m.has_code());
        EXPECT_EQ(m.register_count(), 0);
        EXPECT_TRUE(m.parameter_registers().empty());
    }
}
