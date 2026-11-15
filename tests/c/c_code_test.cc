// The C API for a method's code (bindings/c/dexpp.h): instructions, the
// control-flow graph, try blocks, registers, references and annotations.  See
// c_test_util.hpp.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#include "c_test_util.hpp"
#include "dexpp.h"

using c_test::find_method;
using c_test::open_dex;
using c_test::str;
using c_test::take;

TEST(CApiCode, DecodesInstructionsAndRendersThem)
{
    DexppContext *ctx = open_dex("tests/data/classes.dex");
    ASSERT_NE(ctx, nullptr);
    DexppMethod *main = find_method(ctx, "LTestDex;", "main");
    ASSERT_NE(main, nullptr);

    size_t count = 0;
    DexppInstruction *insns = dexpp_method_instructions(main, &count);
    ASSERT_EQ(count, 5u);
    // sget-object v1, field@0; invoke-static {}, method@1; move-result-object v0;
    // invoke-virtual {v1, v0}, method@3; return-void
    EXPECT_EQ(insns[0].kind, DEXPP_INSN_FIELD);
    EXPECT_EQ(insns[0].offset, 0u);
    EXPECT_EQ(insns[0].is_static, 1);
    EXPECT_EQ(insns[0].is_write, 0);
    EXPECT_EQ(insns[0].has_object_reg, 0);
    EXPECT_EQ(insns[1].kind, DEXPP_INSN_INVOKE);
    EXPECT_EQ(insns[1].arg_count, 0u);
    EXPECT_EQ(insns[2].kind, DEXPP_INSN_MOVE);
    EXPECT_EQ(insns[3].kind, DEXPP_INSN_INVOKE);
    ASSERT_EQ(insns[3].arg_count, 2u);
    EXPECT_EQ(insns[3].arg_regs[0], 1u);
    EXPECT_EQ(insns[3].arg_regs[1], 0u);
    EXPECT_EQ(insns[4].kind, DEXPP_INSN_RETURN);
    EXPECT_EQ(insns[4].has_reg, 0);

    std::vector<std::string> text;
    for (size_t i = 0; i < count; ++i)
        text.push_back(take(dexpp_instruction_to_string(&insns[i])));
    EXPECT_EQ(text[0], "sget-object v1, field@0");
    EXPECT_EQ(text[3], "invoke-virtual method@3 {v1, v0}");
    EXPECT_EQ(text[4], "return-void");
    EXPECT_STREQ(dexpp_opcode_name(insns[3].opcode), "invoke-virtual");
    EXPECT_STREQ(dexpp_opcode_name(0x00), "nop");
    EXPECT_STREQ(dexpp_opcode_name(0x73), "unused-73");

    dexpp_instruction_array_free(insns, count);
    dexpp_method_free(main);
    dexpp_context_free(ctx);
}

TEST(CApiCode, RendersEveryInstructionOfEveryFixtureLikeTheCppApi)
{
    // to_string rebuilds the C++ instruction from the C struct, so this checks
    // the conversion in both directions for every kind the fixtures hold.
    std::set<int> kinds;
    for (const char *path :
         {"tests/data/classes.dex", "tests/data/fibonacci.dex", "tests/data/trycatch.dex",
          "tests/data/shapes.dex", "tests/data/polymorphic.dex", "tests/data/annotated.dex",
          "tests/data/inherit.dex", "tests/data/multistring.dex", "tests/data/casts.dex"}) {
        SCOPED_TRACE(path);
        DexppContext *ctx = open_dex(path);
        ASSERT_NE(ctx, nullptr);
        size_t class_count = 0;
        DexppClass **classes = dexpp_context_classes(ctx, &class_count);
        for (size_t c = 0; c < class_count; ++c) {
            size_t method_count = 0;
            DexppMethod **methods = dexpp_class_methods(classes[c], &method_count);
            for (size_t m = 0; m < method_count; ++m) {
                size_t n = 0;
                DexppInstruction *insns = dexpp_method_instructions(methods[m], &n);
                for (size_t i = 0; i < n; ++i) {
                    kinds.insert(static_cast<int>(insns[i].kind));
                    std::string rendered = take(dexpp_instruction_to_string(&insns[i]));
                    EXPECT_EQ(rendered.rfind(dexpp_opcode_name(insns[i].opcode), 0), 0u)
                        << rendered;
                }
                dexpp_instruction_array_free(insns, n);
            }
            dexpp_method_array_free(methods, method_count);
        }
        dexpp_class_array_free(classes, class_count);
        dexpp_context_free(ctx);
    }
    EXPECT_GE(kinds.size(), 15u);
}

// A C caller may store any value of the enum's integer type in an enum member:
// a kind from a newer header, or garbage.  The test writes the bytes, as C code
// can; in C++ even a static_cast of an out-of-range value to such an enum is
// undefined.  The sanitizer run checks that the shim never reads the member
// through the enum type.
template <typename E> void store_raw(E &member, std::underlying_type_t<E> value)
{
    std::memcpy(&member, &value, sizeof value);
}

TEST(CApiCode, AnUnknownKindRendersAsItsOpcodeName)
{
    DexppInstruction insn{};
    store_raw(insn.kind, 999);
    insn.opcode = 0x0e;
    EXPECT_EQ(take(dexpp_instruction_to_string(&insn)), "return-void");

    store_raw(insn.kind, static_cast<std::underlying_type_t<DexppInstructionKind>>(-1));
    EXPECT_EQ(take(dexpp_instruction_to_string(&insn)), "return-void");
}

TEST(CApiCode, AnUnknownWidthCountsAsSingle)
{
    DexppInstruction insn{};
    insn.kind = DEXPP_INSN_MOVE;
    insn.opcode = 0x01; // move
    insn.dest = 2;
    insn.src = 3;
    store_raw(insn.width, 7);
    EXPECT_EQ(take(dexpp_instruction_to_string(&insn)), "move v2, v3");
}

TEST(CApiCode, BuildsTheControlFlowGraph)
{
    DexppContext *ctx = open_dex("tests/data/fibonacci.dex");
    ASSERT_NE(ctx, nullptr);
    DexppMethod *compute = find_method(ctx, "LFibonacci;", "compute");
    ASSERT_NE(compute, nullptr);

    size_t count = 0;
    DexppBasicBlock *blocks = dexpp_method_cfg(compute, &count);
    ASSERT_GT(count, 1u);
    EXPECT_EQ(blocks[0].start_offset, 0u);
    std::set<int> kinds;
    for (size_t i = 0; i < count; ++i) {
        EXPECT_EQ(blocks[i].index, i);
        for (size_t e = 0; e < blocks[i].successor_count; ++e) {
            ASSERT_LT(blocks[i].successors[e].target_block, count);
            kinds.insert(static_cast<int>(blocks[i].successors[e].kind));
            // Every edge appears among its target's predecessors.
            const DexppBasicBlock &target = blocks[blocks[i].successors[e].target_block];
            bool listed = false;
            for (size_t p = 0; p < target.predecessor_count; ++p)
                listed = listed || target.predecessors[p] == i;
            EXPECT_TRUE(listed);
        }
    }
    EXPECT_TRUE(kinds.contains(DEXPP_CFG_BRANCH_TAKEN));
    EXPECT_TRUE(kinds.contains(DEXPP_CFG_GOTO));
    dexpp_basic_block_array_free(blocks, count);
    dexpp_method_free(compute);
    dexpp_context_free(ctx);
}

TEST(CApiCode, ReadsTryBlocksAndExceptionEdges)
{
    DexppContext *ctx = open_dex("tests/data/trycatch.dex");
    ASSERT_NE(ctx, nullptr);

    DexppMethod *divide = find_method(ctx, "LTryCatch;", "divide");
    ASSERT_NE(divide, nullptr);
    size_t count = 0;
    DexppTryBlock *tries = dexpp_method_try_blocks(divide, &count);
    ASSERT_EQ(count, 1u);
    EXPECT_LT(tries[0].start_offset, tries[0].end_offset);
    ASSERT_EQ(tries[0].handler_count, 1u);
    EXPECT_STREQ(tries[0].handlers[0].type_descriptor, "Ljava/lang/ArithmeticException;");
    EXPECT_EQ(tries[0].has_catch_all, 0);
    dexpp_try_block_array_free(tries, count);

    size_t block_count = 0;
    DexppBasicBlock *blocks = dexpp_method_cfg(divide, &block_count);
    bool exception_edge = false;
    for (size_t i = 0; i < block_count; ++i)
        for (size_t e = 0; e < blocks[i].successor_count; ++e)
            if (blocks[i].successors[e].kind == DEXPP_CFG_EXCEPTION) {
                exception_edge = true;
                EXPECT_STREQ(blocks[i].successors[e].catch_type, "Ljava/lang/ArithmeticException;");
                EXPECT_EQ(blocks[blocks[i].successors[e].target_block].is_handler_entry, 1);
            }
            else {
                EXPECT_EQ(blocks[i].successors[e].catch_type, nullptr);
            }
    EXPECT_TRUE(exception_edge);
    dexpp_basic_block_array_free(blocks, block_count);
    dexpp_method_free(divide);

    DexppMethod *safe = find_method(ctx, "LTryCatch;", "safeParse");
    ASSERT_NE(safe, nullptr);
    tries = dexpp_method_try_blocks(safe, &count);
    bool catch_all = false;
    for (size_t i = 0; i < count; ++i)
        catch_all = catch_all || tries[i].has_catch_all == 1;
    EXPECT_TRUE(catch_all);
    dexpp_try_block_array_free(tries, count);
    // The catch-all handler gets edges of its own kind, with no catch type.
    blocks = dexpp_method_cfg(safe, &block_count);
    bool catch_all_edge = false;
    for (size_t i = 0; i < block_count; ++i)
        for (size_t e = 0; e < blocks[i].successor_count; ++e)
            if (blocks[i].successors[e].kind == DEXPP_CFG_EXCEPTION_CATCH_ALL) {
                catch_all_edge = true;
                EXPECT_EQ(blocks[i].successors[e].catch_type, nullptr);
                EXPECT_EQ(blocks[blocks[i].successors[e].target_block].is_handler_entry, 1);
            }
    EXPECT_TRUE(catch_all_edge);
    dexpp_basic_block_array_free(blocks, block_count);
    dexpp_method_free(safe);
    dexpp_context_free(ctx);
}

TEST(CApiCode, ResolvesParameterRegisters)
{
    DexppContext *ctx = open_dex("tests/data/fibonacci.dex");
    ASSERT_NE(ctx, nullptr);

    DexppMethod *compute = find_method(ctx, "LFibonacci;", "compute");
    ASSERT_NE(compute, nullptr);
    EXPECT_EQ(dexpp_method_register_count(compute), 11u);
    size_t count = 0;
    DexppParameterRegister *regs = dexpp_method_parameter_registers(compute, &count);
    ASSERT_EQ(count, 2u);
    EXPECT_EQ(regs[0].is_this, 1);
    EXPECT_EQ(regs[0].reg, 9u);
    EXPECT_STREQ(regs[0].type, "LFibonacci;");
    EXPECT_EQ(regs[1].reg, 10u);
    EXPECT_STREQ(regs[1].type, "I");
    EXPECT_EQ(regs[1].is_wide, 0);
    dexpp_parameter_register_array_free(regs, count);
    dexpp_method_free(compute);

    DexppMethod *classify = find_method(ctx, "LFibonacci;", "classify");
    ASSERT_NE(classify, nullptr);
    regs = dexpp_method_parameter_registers(classify, &count);
    ASSERT_EQ(count, 1u);
    EXPECT_EQ(regs[0].is_wide, 1);
    EXPECT_EQ(regs[0].reg, 3u);
    EXPECT_STREQ(regs[0].type, "J");
    dexpp_parameter_register_array_free(regs, count);
    dexpp_method_free(classify);
    dexpp_context_free(ctx);
}

TEST(CApiCode, ListsCallsFieldAccessesStringsAndTypes)
{
    DexppContext *ctx = open_dex("tests/data/classes.dex");
    ASSERT_NE(ctx, nullptr);
    DexppMethod *main = find_method(ctx, "LTestDex;", "main");
    ASSERT_NE(main, nullptr);

    size_t count = 0;
    DexppMethodCall *calls = dexpp_method_calls(main, &count);
    ASSERT_EQ(count, 2u);
    EXPECT_STREQ(calls[0].target.name, "helloDex");
    EXPECT_EQ(calls[0].kind, DEXPP_INVOKE_STATIC);
    EXPECT_STREQ(calls[1].target.class_descriptor, "Ljava/io/PrintStream;");
    EXPECT_STREQ(calls[1].target.name, "println");
    EXPECT_STREQ(calls[1].target.proto, "(Ljava/lang/String;)V");
    EXPECT_EQ(calls[1].kind, DEXPP_INVOKE_VIRTUAL);
    dexpp_method_call_array_free(calls, count);

    DexppFieldAccess *accesses = dexpp_method_field_accesses(main, &count);
    ASSERT_EQ(count, 1u);
    EXPECT_STREQ(accesses[0].field.name, "out");
    EXPECT_STREQ(accesses[0].field.type, "Ljava/io/PrintStream;");
    EXPECT_EQ(accesses[0].is_static, 1);
    EXPECT_EQ(accesses[0].is_write, 0);
    dexpp_field_access_array_free(accesses, count);
    dexpp_method_free(main);

    DexppMethod *hello = find_method(ctx, "LTestDex;", "helloDex");
    ASSERT_NE(hello, nullptr);
    DexppStringLoad *loads = dexpp_method_string_loads(hello, &count);
    ASSERT_EQ(count, 1u);
    EXPECT_EQ(str(loads[0].value), "Hello Dex++!!");
    dexpp_string_load_array_free(loads, count);
    dexpp_method_free(hello);
    dexpp_context_free(ctx);

    // A string constant with a NUL byte keeps it.
    ctx = open_dex("tests/data/multistring.dex");
    ASSERT_NE(ctx, nullptr);
    DexppMethod *clinit = find_method(ctx, "LMultiStringTest;", "<clinit>");
    ASSERT_NE(clinit, nullptr);
    loads = dexpp_method_string_loads(clinit, &count);
    bool found = false;
    for (size_t i = 0; i < count; ++i)
        found = found || str(loads[i].value) == std::string("test\0embedded", 13);
    EXPECT_TRUE(found);
    dexpp_string_load_array_free(loads, count);
    dexpp_method_free(clinit);
    dexpp_context_free(ctx);

    ctx = open_dex("tests/data/polymorphic.dex");
    ASSERT_NE(ctx, nullptr);
    DexppMethod *describe = find_method(ctx, "LDog;", "describe");
    ASSERT_NE(describe, nullptr);
    DexppTypeUse *uses = dexpp_method_type_uses(describe, &count);
    ASSERT_GE(count, 1u);
    EXPECT_STREQ(uses[0].descriptor, "Ljava/lang/StringBuilder;");
    EXPECT_EQ(uses[0].kind, DEXPP_TYPE_USE_NEW_INSTANCE);
    dexpp_type_use_array_free(uses, count);
    dexpp_method_free(describe);
    dexpp_context_free(ctx);
}

TEST(CApiCode, ReadsAnnotationsWithEveryValueShape)
{
    DexppContext *ctx = open_dex("tests/data/annotated.dex");
    ASSERT_NE(ctx, nullptr);
    DexppClass *cls = dexpp_context_find_class(ctx, "LAnnotated;");
    ASSERT_NE(cls, nullptr);

    size_t count = 0;
    DexppAnnotation *annotations = dexpp_class_annotations(cls, &count);
    const DexppAnnotation *marker = nullptr;
    for (size_t i = 0; i < count; ++i)
        if (std::string(annotations[i].type_descriptor) == "LMarker;")
            marker = &annotations[i];
    ASSERT_NE(marker, nullptr);
    EXPECT_EQ(marker->visibility, DEXPP_VISIBILITY_RUNTIME);
    std::map<std::string, const DexppValue *> elements;
    for (size_t i = 0; i < marker->element_count; ++i)
        elements[marker->elements[i].name] = &marker->elements[i].value;
    ASSERT_TRUE(elements.contains("name"));
    EXPECT_EQ(elements["name"]->kind, DEXPP_VALUE_STRING);
    EXPECT_EQ(str(elements["name"]->string_value), "cls");
    ASSERT_TRUE(elements.contains("level"));
    EXPECT_EQ(elements["level"]->int_value, 3);
    ASSERT_TRUE(elements.contains("tags"));
    ASSERT_EQ(elements["tags"]->kind, DEXPP_VALUE_ARRAY);
    ASSERT_EQ(elements["tags"]->element_count, 2u);
    EXPECT_EQ(str(elements["tags"]->elements[0].string_value), "a");
    EXPECT_EQ(str(elements["tags"]->elements[1].string_value), "b");
    EXPECT_FALSE(elements.contains("target")); // left at its default
    dexpp_annotation_array_free(annotations, count);
    dexpp_class_free(cls);

    DexppMethod *greet = find_method(ctx, "LAnnotated;", "greet");
    ASSERT_NE(greet, nullptr);
    annotations = dexpp_method_annotations(greet, &count);
    bool saw_target = false;
    for (size_t i = 0; i < count; ++i)
        for (size_t e = 0; e < annotations[i].element_count; ++e)
            if (std::string(annotations[i].elements[e].name) == "target") {
                saw_target = true;
                EXPECT_EQ(annotations[i].elements[e].value.kind, DEXPP_VALUE_TYPE);
                EXPECT_STREQ(annotations[i].elements[e].value.type_descriptor,
                             "Ljava/lang/String;");
            }
    EXPECT_TRUE(saw_target);
    dexpp_annotation_array_free(annotations, count);
    dexpp_method_free(greet);

    DexppMethod *old = find_method(ctx, "LAnnotated;", "old");
    ASSERT_NE(old, nullptr);
    annotations = dexpp_method_annotations(old, &count);
    bool deprecated = false;
    for (size_t i = 0; i < count; ++i)
        deprecated =
            deprecated || std::string(annotations[i].type_descriptor) == "Ljava/lang/Deprecated;";
    EXPECT_TRUE(deprecated);
    dexpp_annotation_array_free(annotations, count);
    dexpp_method_free(old);
    dexpp_context_free(ctx);
}

TEST(CApiCode, ReadsSystemAnnotationsWithNestedValues)
{
    // System annotations (dalvik.annotation.*) carry arrays, types and
    // methods; walking them all exercises the value conversion and its free.
    for (const char *path : {"tests/data/annotated.dex", "tests/data/inherit.dex",
                             "tests/data/polymorphic.dex", "tests/data/shapes.dex"}) {
        DexppContext *ctx = open_dex(path);
        ASSERT_NE(ctx, nullptr);
        size_t class_count = 0;
        DexppClass **classes = dexpp_context_classes(ctx, &class_count);
        for (size_t c = 0; c < class_count; ++c) {
            size_t n = 0;
            DexppAnnotation *annotations = dexpp_class_annotations(classes[c], &n);
            for (size_t i = 0; i < n; ++i)
                EXPECT_NE(annotations[i].type_descriptor, nullptr);
            dexpp_annotation_array_free(annotations, n);
        }
        dexpp_class_array_free(classes, class_count);
        dexpp_context_free(ctx);
    }
}
