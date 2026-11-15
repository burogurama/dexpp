#include <gtest/gtest.h>

#include <format>
#include <map>
#include <numeric>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "dex.hpp"

using namespace dex;

class InstructionsTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        auto result = AnalysisContext::from_dex("tests/data/trycatch.dex");
        ASSERT_TRUE(result.has_value()) << result.error().message;
        ctx_.emplace(std::move(*result));

        auto cls = ctx_->find_class("LTryCatch;");
        ASSERT_TRUE(cls.has_value());
        methods_ = cls->methods();
        ASSERT_EQ(methods_.size(), 3u);

        // method order: [0] <init>()V, [1] divide(II)I, [2] safeParse(String)String
    }

    std::optional<AnalysisContext> ctx_;
    std::vector<Method> methods_;
};

TEST(InstructionsStandaloneTest, AbstractMethodReturnsEmpty)
{
    // Use shapes.dex — LShape; is an interface whose methods are abstract (code_off==0)
    auto ctx = AnalysisContext::from_dex("tests/data/shapes.dex");
    ASSERT_TRUE(ctx.has_value());

    auto cls = ctx->find_class("LShape;");
    ASSERT_TRUE(cls.has_value());

    // Every method of an interface is abstract; all should return empty
    for (const auto &m : cls->methods()) {
        EXPECT_TRUE(m.instructions().empty())
            << "Expected empty instructions for abstract method " << m.name();
    }
}

TEST_F(InstructionsTest, NonAbstractMethodHasInstructions)
{
    // divide is a concrete method
    auto insns = methods_[1].instructions();
    EXPECT_FALSE(insns.empty());
}

TEST_F(InstructionsTest, FirstInstructionOffsetIsZero)
{
    auto insns = methods_[1].instructions();
    ASSERT_FALSE(insns.empty());
    EXPECT_EQ(base_of(insns[0]).offset, 0u);
}

TEST_F(InstructionsTest, InstructionsAreContiguous)
{
    for (const auto &method : methods_) {
        auto insns = method.instructions();
        for (std::size_t i = 1; i < insns.size(); ++i) {
            const auto &prev = base_of(insns[i - 1]);
            const auto &curr = base_of(insns[i]);
            EXPECT_EQ(prev.offset + prev.size, curr.offset)
                << "Gap before instruction " << i << " in method " << method.name();
        }
    }
}

TEST_F(InstructionsTest, DivideContainsReturnVariant)
{
    auto insns = methods_[1].instructions();
    ASSERT_FALSE(insns.empty());
    EXPECT_TRUE(std::holds_alternative<ReturnInstruction>(insns.back()));
}

TEST_F(InstructionsTest, GotoPresent)
{
    auto insns = methods_[2].instructions();
    bool found = false;
    for (const auto &insn : insns) {
        if (std::holds_alternative<GotoInstruction>(insn)) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "No goto instruction found in safeParse";
}

TEST_F(InstructionsTest, GotoBranchOffsetNonZero)
{
    auto insns = methods_[2].instructions();
    for (const auto &insn : insns) {
        if (auto *p = std::get_if<GotoInstruction>(&insn)) {
            EXPECT_NE(p->branch_offset, 0);
            return;
        }
    }
    FAIL() << "No goto instruction found in safeParse";
}

TEST_F(InstructionsTest, InvokeHasMethodIndex)
{
    auto insns = methods_[2].instructions();
    for (const auto &insn : insns) {
        if (auto *p = std::get_if<InvokeInstruction>(&insn)) {
            // method_ids has 7 entries (indices 0–6)
            EXPECT_LT(p->method_index, 7u);
            return;
        }
    }
    FAIL() << "No InvokeInstruction found in safeParse";
}

TEST_F(InstructionsTest, InvokeHasArgRegisters)
{
    auto insns = methods_[2].instructions();
    for (const auto &insn : insns) {
        if (auto *p = std::get_if<InvokeInstruction>(&insn)) {
            EXPECT_FALSE(p->arg_regs.empty());
            return;
        }
    }
    FAIL() << "No InvokeInstruction found in safeParse";
}

TEST_F(InstructionsTest, MoveInstructionHasRegisters)
{
    // move-result-object after invoke-static stores result
    auto insns = methods_[2].instructions();
    for (const auto &insn : insns) {
        if (auto *p = std::get_if<MoveInstruction>(&insn)) {
            // dest must be a valid register (registers_size = 3 for safeParse)
            EXPECT_LT(p->dest, 3u);
            return;
        }
    }
    FAIL() << "No MoveInstruction found in safeParse";
}

TEST(InstructionsStandaloneTest, WideInstructionHasPairWidth)
{
    // Circle.<init>(String, double) — parameters include a 'double' (wide)
    auto ctx = AnalysisContext::from_dex("tests/data/shapes.dex");
    ASSERT_TRUE(ctx.has_value());

    auto cls = ctx->find_class("LCircle;");
    ASSERT_TRUE(cls.has_value());

    // Find <init>
    std::optional<Method> init_method;
    for (const auto &m : cls->methods()) {
        if (m.name() == "<init>") {
            init_method = m;
            break;
        }
    }
    ASSERT_TRUE(init_method.has_value());

    auto insns = init_method->instructions();
    bool found_wide = false;
    for (const auto &insn : insns) {
        if (auto *p = std::get_if<MoveInstruction>(&insn)) {
            if (p->width == OperandWidth::Pair) {
                found_wide = true;
                break;
            }
        }
        if (auto *p = std::get_if<FieldInstruction>(&insn)) {
            if (p->width == OperandWidth::Pair) {
                found_wide = true;
                break;
            }
        }
    }
    EXPECT_TRUE(found_wide) << "No wide instruction found in Circle.<init>";
}

TEST_F(InstructionsTest, AllInstructionsCovered)
{
    // divide: insns_size = 5
    {
        auto insns = methods_[1].instructions();
        uint32_t total = 0;
        for (const auto &insn : insns)
            total += base_of(insn).size;
        EXPECT_EQ(total, 5u) << "Size mismatch for divide";
    }
    // safeParse: insns_size = 0x21 = 33
    {
        auto insns = methods_[2].instructions();
        uint32_t total = 0;
        for (const auto &insn : insns)
            total += base_of(insn).size;
        EXPECT_EQ(total, 33u) << "Size mismatch for safeParse";
    }
}

TEST_F(InstructionsTest, PayloadsAreSkipped)
{
    for (const auto &method : methods_) {
        for (const auto &insn : method.instructions()) {
            const auto &b = base_of(insn);
            // A payload has opcode 0x00 and high byte != 0
            EXPECT_FALSE(b.opcode == 0x00 && b.size > 1)
                << "Payload pseudo-instruction leaked in " << method.name();
        }
    }
}

TEST_F(InstructionsTest, InstructionsSpanIsStableAcrossCalls)
{
    auto s1 = methods_[1].instructions();
    auto s2 = methods_[1].instructions();
    ASSERT_FALSE(s1.empty());
    EXPECT_EQ(s1.data(), s2.data()) << "instructions() should hit the per-context cache";
    EXPECT_EQ(s1.size(), s2.size());
}

// ---- Shapes the decoder gives each opcode family ----

TEST(InstructionShapesTest, MoveResultAndMoveExceptionHaveNoSource)
{
    // move v1, v2 ; move-result v3 ; move-exception v4
    const std::vector<uint16_t> code = {0x2101, 0x030a, 0x040d};
    auto insns = decode_instructions(code);
    ASSERT_EQ(insns.size(), 3u);
    const auto &move = std::get<MoveInstruction>(insns[0]);
    EXPECT_EQ(move.dest, 1u);
    EXPECT_EQ(move.src, std::optional<uint16_t>(2));
    const auto &result = std::get<MoveInstruction>(insns[1]);
    EXPECT_EQ(result.dest, 3u);
    EXPECT_FALSE(result.src.has_value());
    const auto &exception = std::get<MoveInstruction>(insns[2]);
    EXPECT_EQ(exception.dest, 4u);
    EXPECT_FALSE(exception.src.has_value());
    EXPECT_EQ(to_string(insns[0]), "move v1, v2");
    EXPECT_EQ(to_string(insns[1]), "move-result v3");
    EXPECT_EQ(to_string(insns[2]), "move-exception v4");
}

TEST(InstructionShapesTest, UnusedOpcodeIsInvalid)
{
    // unused-3e ; unused-f9 ; return-void
    const std::vector<uint16_t> code = {0x123e, 0x00f9, 0x000e};
    auto insns = decode_instructions(code);
    ASSERT_EQ(insns.size(), 3u);
    for (std::size_t i = 0; i < 2; ++i) {
        ASSERT_TRUE(std::holds_alternative<InvalidInstruction>(insns[i])) << i;
        EXPECT_EQ(base_of(insns[i]).offset, i);
        EXPECT_EQ(base_of(insns[i]).size, 1u);
    }
    EXPECT_EQ(base_of(insns[0]).opcode, 0x3eu);
    EXPECT_EQ(base_of(insns[1]).opcode, 0xf9u);
    EXPECT_EQ(to_string(insns[0]), "unused-3e");
    EXPECT_TRUE(std::holds_alternative<ReturnInstruction>(insns[2]));
    EXPECT_FALSE(is_terminator(insns[0]));
}

TEST(InstructionShapesTest, ArrayLength)
{
    // array-length v1, v5
    const std::vector<uint16_t> code = {0x5121};
    auto insns = decode_instructions(code);
    ASSERT_EQ(insns.size(), 1u);
    const auto &length = std::get<ArrayLengthInstruction>(insns[0]);
    EXPECT_EQ(length.dest, 1u);
    EXPECT_EQ(length.array_reg, 5u);
    EXPECT_EQ(to_string(insns[0]), "array-length v1, v5");
}

// handles.dex (tests/model/handles_fixture_test.cc builds it):
//   run:    invoke-polymorphic {v4, v5}, MethodHandle.invokeExact, proto
//           move-result-object v0 ; array-length v1, v5
//           const-method-handle v2 ; const-method-type v3
//           invoke-custom {v1} ; return-object v0
//   spread: invoke-polymorphic/range {v0 .. v5} ; return-void
class HandlesInstructionsTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        auto result = AnalysisContext::from_dex("tests/data/handles.dex");
        ASSERT_TRUE(result.has_value()) << result.error().message;
        ctx_.emplace(std::move(*result));
        auto cls = ctx_->find_class("LHandles;");
        ASSERT_TRUE(cls.has_value());
        for (const Method &m : cls->methods())
            methods_.emplace(std::string(m.name()), m);
    }

    std::optional<AnalysisContext> ctx_;
    std::map<std::string, Method> methods_;
};

TEST_F(HandlesInstructionsTest, RunDecodesEveryNewShape)
{
    auto insns = methods_.at("run").instructions();
    ASSERT_EQ(insns.size(), 7u);

    const auto &poly = std::get<InvokePolymorphicInstruction>(insns[0]);
    EXPECT_EQ(poly.base.opcode, 0xfau);
    EXPECT_EQ(poly.arg_regs, (std::vector<uint16_t>{4, 5}));
    EXPECT_EQ(to_string(insns[0]), std::format("invoke-polymorphic method@{} proto@{} {{v4, v5}}",
                                               poly.method_index, poly.proto_index));

    const auto &result = std::get<MoveInstruction>(insns[1]);
    EXPECT_FALSE(result.src.has_value());

    const auto &length = std::get<ArrayLengthInstruction>(insns[2]);
    EXPECT_EQ(length.dest, 1u);
    EXPECT_EQ(length.array_reg, 5u);

    const auto &handle = std::get<ConstMethodHandleInstruction>(insns[3]);
    EXPECT_EQ(handle.dest, 2u);
    EXPECT_EQ(to_string(insns[3]),
              std::format("const-method-handle v2, method_handle@{}", handle.method_handle_index));

    const auto &type = std::get<ConstMethodTypeInstruction>(insns[4]);
    EXPECT_EQ(type.dest, 3u);
    EXPECT_EQ(to_string(insns[4]), std::format("const-method-type v3, proto@{}", type.proto_index));

    EXPECT_TRUE(std::holds_alternative<InvokeCustomInstruction>(insns[5]));
}

TEST_F(HandlesInstructionsTest, PolymorphicRangeKeepsTheProto)
{
    auto run = methods_.at("run").instructions();
    auto spread = methods_.at("spread").instructions();
    ASSERT_FALSE(spread.empty());
    const auto &range = std::get<InvokePolymorphicInstruction>(spread[0]);
    EXPECT_EQ(range.base.opcode, 0xfbu);
    EXPECT_EQ(range.arg_regs, (std::vector<uint16_t>{0, 1, 2, 3, 4, 5}));
    // invokeExact and invoke: two methods; two different call-site prototypes.
    const auto &poly = std::get<InvokePolymorphicInstruction>(run[0]);
    EXPECT_NE(range.method_index, poly.method_index);
    EXPECT_NE(range.proto_index, poly.proto_index);
}

TEST_F(HandlesInstructionsTest, PolymorphicCallsAreForwardAndReverseReferences)
{
    const MethodId invoke_exact{"Ljava/lang/invoke/MethodHandle;", "invokeExact",
                                "([Ljava/lang/Object;)Ljava/lang/Object;"};
    auto calls = methods_.at("run").calls();
    ASSERT_EQ(calls.size(), 1u); // invoke-custom calls no method by name
    EXPECT_EQ(calls[0].target, invoke_exact);
    EXPECT_EQ(calls[0].kind, InvokeKind::Polymorphic);
    EXPECT_EQ(calls[0].code_offset, 0u);

    auto sites = ctx_->xrefs().method_refs(invoke_exact);
    ASSERT_EQ(sites.size(), 1u);
    EXPECT_EQ(sites[0].kind, InvokeKind::Polymorphic);
    EXPECT_EQ(ctx_->xrefs().referrer_id(sites[0].referrer).name, "run");
}
