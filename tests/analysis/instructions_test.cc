#include <gtest/gtest.h>

#include <numeric>

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
