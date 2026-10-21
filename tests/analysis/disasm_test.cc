// Tests for opcode_name / to_string (instruction rendering) and the
// whole-program string-pool enumeration AnalysisContext::strings().

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "dex.hpp"

using namespace dex;

namespace {

// Find a method by name on a class; fails the calling test if absent.
Method method_named(const Class &cls, std::string_view name)
{
    for (const auto &m : cls.methods())
        if (m.name() == name)
            return m;
    throw std::runtime_error("method not found: " + std::string(name));
}

} // namespace

TEST(OpcodeNameTest, KnownOpcodes)
{
    EXPECT_EQ(opcode_name(0x00), "nop");
    EXPECT_EQ(opcode_name(0x0e), "return-void");
    EXPECT_EQ(opcode_name(0x1a), "const-string");
    EXPECT_EQ(opcode_name(0x6e), "invoke-virtual");
    EXPECT_EQ(opcode_name(0x71), "invoke-static");
    EXPECT_EQ(opcode_name(0xff), "const-method-type");
}

TEST(OpcodeNameTest, UnusedOpcodesAreNamedNotEmpty)
{
    // 0x73 is unused in the Dalvik spec; must still yield a stable label.
    EXPECT_EQ(opcode_name(0x73), "unused-73");
    EXPECT_FALSE(opcode_name(0x3e).empty());
}

// helloDex(): const-string vN, string@K ; return-object vN
TEST(ToStringTest, RendersConstStringAndReturn)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value()) << ctx.error().message;
    auto cls = ctx->find_class("LTestDex;");
    ASSERT_TRUE(cls.has_value());

    Method hello = method_named(*cls, "helloDex");
    auto insns = hello.instructions();
    ASSERT_GE(insns.size(), 2u);

    // First instruction is a const-string load into some register.
    std::string first = to_string(insns[0]);
    EXPECT_TRUE(first.starts_with("const-string v")) << first;
    EXPECT_NE(first.find("string@"), std::string::npos) << first;

    // Last is return-object.
    EXPECT_TRUE(to_string(insns.back()).starts_with("return-object v"));
}

// main(): sget-object, invoke-static {} (no args), move-result-object, invoke-virtual
TEST(ToStringTest, RendersInvokeAndFieldOperands)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());
    auto cls = ctx->find_class("LTestDex;");
    ASSERT_TRUE(cls.has_value());

    Method main_m = method_named(*cls, "main");
    std::vector<std::string> rendered;
    for (const auto &insn : main_m.instructions())
        rendered.push_back(to_string(insn));

    // sget-object reads a static field.
    bool sget = std::any_of(rendered.begin(), rendered.end(), [](const std::string &s) {
        return s.starts_with("sget-object v") && s.find("field@") != std::string::npos;
    });
    EXPECT_TRUE(sget);

    // An argument-less invoke renders an empty register list "{}".
    bool empty_args = std::any_of(rendered.begin(), rendered.end(), [](const std::string &s) {
        return s.starts_with("invoke-static method@") && s.ends_with("{}");
    });
    EXPECT_TRUE(empty_args);

    // A virtual call with arguments renders "{v.., v..}".
    bool with_args = std::any_of(rendered.begin(), rendered.end(), [](const std::string &s) {
        return s.starts_with("invoke-virtual method@") && s.find("{v") != std::string::npos;
    });
    EXPECT_TRUE(with_args);
}

TEST(StringsTest, ContainsPoolStringsBeyondBytecode)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());

    auto strings = ctx->strings();
    ASSERT_FALSE(strings.empty());

    // The literal loaded by helloDex is in the pool...
    EXPECT_NE(std::find(strings.begin(), strings.end(), "Hello Dex++!!"), strings.end());
    // ...and so are member/type names that no const-string instruction loads.
    EXPECT_NE(std::find(strings.begin(), strings.end(), "helloDex"), strings.end());
    EXPECT_NE(std::find(strings.begin(), strings.end(), "LTestDex;"), strings.end());

    // strings() is a superset of the bytecode-referenced strings.
    for (auto ref : ctx->xrefs().referenced_strings())
        EXPECT_NE(std::find(strings.begin(), strings.end(), ref), strings.end()) << ref;
}

TEST(StringsTest, MultiDexConcatenatesPools)
{
    auto one = AnalysisContext::from_dex("tests/data/classes.dex");
    auto two =
        AnalysisContext::from_dex_files({"tests/data/classes.dex", "tests/data/fibonacci.dex"});
    ASSERT_TRUE(one.has_value());
    ASSERT_TRUE(two.has_value());

    // Loading a second DEX can only add strings.
    EXPECT_GT(two->strings().size(), one->strings().size());
}
