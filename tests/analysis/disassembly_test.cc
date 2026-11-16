// Method::disassemble(): instructions as text with their references named.
// The fixtures check real code end to end; the detail-level tests feed
// hand-made code units to the renderer to reach what the fixtures lack
// (escapes, damaged indices and payloads, every payload width).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "analysis/disassembly.hpp"
#include "dex.hpp"
#include "model/model.hpp"

using namespace dex;

namespace {

Method method_named(const AnalysisContext &ctx, std::string_view cls, std::string_view name)
{
    std::optional<Class> c = ctx.find_class(cls);
    if (!c)
        throw std::runtime_error("class not found: " + std::string(cls));
    for (const Method &m : c->methods())
        if (m.name() == name)
            return m;
    throw std::runtime_error("method not found: " + std::string(name));
}

std::vector<std::string> texts(const Method &m)
{
    std::vector<std::string> out;
    for (const DisassemblyLine &line : m.disassemble())
        out.push_back(line.text);
    return out;
}

// Names for indices 0 to 9 of every table; any other index is out of range.
class FakeNames final : public detail::PoolNames
{
  public:
    std::optional<std::string> string(uint32_t i) const override
    {
        if (i == 1)
            return std::string("tab\there \"quoted\" back\\slash\x01 \x7f end\n");
        if (i == 2)
            return std::string("caf\xc3\xa9 \xe2\x80\xae"
                               "evil \xe2\x80\x8b zero \xed\xa0\x80 lone");
        if (i == 3)
            return std::string("bad \xff utf8 \xc3");
        return pick(i, "s");
    }
    std::optional<std::string> type(uint32_t i) const override { return pick(i, "LT;"); }
    std::optional<std::string> field(uint32_t i) const override { return pick(i, "LC;->f:I"); }
    std::optional<std::string> method(uint32_t i) const override { return pick(i, "LC;->m()V"); }
    std::optional<std::string> proto(uint32_t i) const override { return pick(i, "(I)V"); }
    std::optional<std::string> method_handle(uint32_t i) const override
    {
        return pick(i, "invoke-static@LC;->m()V");
    }
    std::optional<std::string> call_site(uint32_t i) const override
    {
        return pick(i, "(\"run\", ()V)@LC;->b()V");
    }

  private:
    static std::optional<std::string> pick(uint32_t i, const std::string &name)
    {
        if (i >= 10)
            return std::nullopt;
        return i == 0 ? name : name + std::to_string(i);
    }
};

// Every instruction of @p code, rendered with FakeNames.
std::vector<std::string> render(const std::vector<uint16_t> &code)
{
    FakeNames names;
    std::vector<std::string> out;
    for (const Instruction &insn : decode_instructions(code))
        out.push_back(detail::disassemble_instruction(insn, code, names));
    return out;
}

} // namespace

TEST(DisassembleTest, NamesEveryReferenceInRealCode)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value()) << ctx.error().message;

    EXPECT_EQ(texts(method_named(*ctx, "LTestDex;", "helloDex")),
              (std::vector<std::string>{"const-string v0, \"Hello Dex++!!\"", "return-object v0"}));
    EXPECT_EQ(texts(method_named(*ctx, "LTestDex;", "main")),
              (std::vector<std::string>{
                  "sget-object v1, Ljava/lang/System;->out:Ljava/io/PrintStream;",
                  "invoke-static {}, LTestDex;->helloDex()Ljava/lang/String;",
                  "move-result-object v0",
                  "invoke-virtual {v1, v0}, Ljava/io/PrintStream;->println(Ljava/lang/String;)V",
                  "return-void",
              }));

    auto shapes = AnalysisContext::from_dex("tests/data/shapes.dex");
    ASSERT_TRUE(shapes.has_value());
    EXPECT_EQ(texts(method_named(*shapes, "LCircle;", "area")),
              (std::vector<std::string>{
                  "iget-wide v0, v4, LCircle;->radius:D",
                  "const-wide v2, 0x400921fb54442d18L",
                  "mul-double v2, v2, v0",
                  "mul-double v2, v2, v0",
                  "return-wide v2",
              }));
    EXPECT_TRUE(method_named(*shapes, "LShape;", "area").disassemble().empty())
        << "an abstract method has no code";

    auto casts = AnalysisContext::from_dex("tests/data/casts.dex");
    ASSERT_TRUE(casts.has_value());
    std::vector<std::string> make_array = texts(method_named(*casts, "LCasts;", "makeArray"));
    ASSERT_GE(make_array.size(), 2u);
    EXPECT_EQ(make_array[1], "new-array v0, v0, [Ljava/lang/String;");
    EXPECT_EQ(texts(method_named(*casts, "LCasts;", "doCast"))[1],
              "check-cast v0, Ljava/lang/String;");
}

TEST(DisassembleTest, MethodHandlesTypesAndCallSites)
{
    auto ctx = AnalysisContext::from_dex("tests/data/handles.dex");
    ASSERT_TRUE(ctx.has_value()) << ctx.error().message;
    EXPECT_EQ(texts(method_named(*ctx, "LHandles;", "run")),
              (std::vector<std::string>{
                  "invoke-polymorphic {v4, v5}, "
                  "Ljava/lang/invoke/MethodHandle;->invokeExact([Ljava/lang/Object;)"
                  "Ljava/lang/Object;, ([Ljava/lang/Object;)Ljava/lang/Object;",
                  "move-result-object v0",
                  "array-length v1, v5",
                  "const-method-handle v2, invoke-static@LHandles;->target(I)I",
                  "const-method-type v3, (I)I",
                  "invoke-custom {v1}, call_site@0(\"apply\", (I)V)@LHandles;->bootstrap("
                  "Ljava/lang/invoke/MethodHandles$Lookup;Ljava/lang/String;"
                  "Ljava/lang/invoke/MethodType;)Ljava/lang/invoke/CallSite;",
                  "return-object v0",
              }));
    EXPECT_EQ(texts(method_named(*ctx, "LHandles;", "spread"))[0],
              "invoke-polymorphic/range {v0 .. v5}, "
              "Ljava/lang/invoke/MethodHandle;->invoke([Ljava/lang/Object;)Ljava/lang/Object;, "
              "(IIIII)V");
}

TEST(DisassembleTest, SwitchTablesAreWrittenOut)
{
    auto ctx = AnalysisContext::from_dex("tests/data/switches.dex");
    ASSERT_TRUE(ctx.has_value()) << ctx.error().message;
    std::vector<DisassemblyLine> lines = method_named(*ctx, "LSwitches;", "packed").disassemble();
    ASSERT_FALSE(lines.empty());
    EXPECT_EQ(lines[0].text,
              "packed-switch v0, {0xa -> 000e, 0xb -> 000b, 0xc -> 0008, 0xd -> 0005}");
    // Each case target is the offset of a line.
    for (uint32_t target : {0x0eu, 0x0bu, 0x08u, 0x05u})
        EXPECT_TRUE(std::any_of(lines.begin(), lines.end(), [&](const DisassemblyLine &l) {
            return l.offset == target;
        })) << target;
}

TEST(DisassembleTest, OneLinePerInstructionInEveryFixture)
{
    std::size_t lines = 0;
    for (const char *dir : {"tests/data", "tests/data/local"}) {
        if (!std::filesystem::is_directory(dir))
            continue;
        for (const auto &entry : std::filesystem::directory_iterator(dir)) {
            const std::string path = entry.path().generic_string();
            if (!path.ends_with(".dex") && !path.ends_with(".apk"))
                continue;
            auto ctx = path.ends_with(".dex") ? AnalysisContext::from_dex(path)
                                              : AnalysisContext::from_apk(path);
            if (!ctx.has_value())
                continue;
            for (const Class &cls : ctx->classes())
                for (const Method &m : cls.methods()) {
                    std::span<const Instruction> insns = m.instructions();
                    std::vector<DisassemblyLine> text = m.disassemble();
                    ASSERT_EQ(text.size(), insns.size()) << path;
                    for (std::size_t k = 0; k < insns.size(); ++k) {
                        EXPECT_EQ(text[k].offset, base_of(insns[k]).offset) << path;
                        EXPECT_TRUE(text[k].text.starts_with(opcode_name(base_of(insns[k]).opcode)))
                            << path << ": " << text[k].text;
                        EXPECT_EQ(text[k].text.find_first_of(std::string("\n\r\0", 3)),
                                  std::string::npos)
                            << path << ": " << text[k].text;
                    }
                    lines += text.size();
                }
        }
    }
    EXPECT_GT(lines, 200u);
}

TEST(DisassembleTest, CallSiteArgumentsTablesAndEscapesEndToEnd)
{
    // What the fixtures lack, written as a DEX file by the model: a call site
    // with extra arguments of every kind, a sparse switch, array data and a
    // string that needs escapes.
    namespace m = dex::model;
    m::Dex dex;
    dex.version = 39;
    m::ClassDef &c = dex.classes.emplace_back();
    c.type = "LDis;";
    c.access = AccessFlags::Public;
    c.superclass = "Ljava/lang/Object;";
    m::MethodDef &f = c.methods.emplace_back();
    f.name = "f";
    f.proto = "(I)V";
    f.access = AccessFlags::Public | AccessFlags::Static;
    m::Code &code = f.code.emplace();
    code.registers = 2;

    m::CallSite site;
    site.bootstrap = m::MethodHandle{
        MethodHandleKind::InvokeStatic,
        MethodId{"LDis;", "boot",
                 "(Ljava/lang/invoke/MethodHandles$Lookup;Ljava/lang/String;"
                 "Ljava/lang/invoke/MethodType;[Ljava/lang/Object;)Ljava/lang/invoke/CallSite;"}};
    site.method_name = "go";
    site.method_type = "()V";
    site.extra_args = {
        m::Value{m::Value::Int{-2}},
        m::Value{m::Value::Long{16}},
        m::Value{m::Value::Byte{1}},
        m::Value{m::Value::Short{-1}},
        m::Value{m::Value::Char{u'\''}},
        m::Value{m::Value::Float{1.5f}},
        m::Value{m::Value::Double{-0.25}},
        m::Value{m::Value::Boolean{true}},
        m::Value{m::Value::Null{}},
        m::Value{m::Value::String{"a\tb"}},
        m::Value{m::Value::Type{"[I"}},
        m::Value{m::Value::MethodType{"(J)Z"}},
        m::Value{m::MethodHandle{MethodHandleKind::StaticGet, FieldId{"LDis;", "x", "I"}}},
        m::Value{m::Value::Enum{FieldId{"LE;", "A", "LE;"}}},
        m::Value{m::Value::Array{{m::Value{m::Value::Int{1}}, m::Value{m::Value::Int{2}}}}},
    };
    const m::Label one{1}, two{2}, end{3};
    code.items = {
        m::Insn{m::ConstString{0, std::string("line\u2028sep \"q\"")}},
        m::Insn{m::InvokeCustom{m::RegList{0}, site}},
        m::Insn{m::FillArrayData{0, 4, {1, 0, 0, 0, 0xff, 0xff, 0xff, 0xff}}},
        m::Insn{m::Switch{1, {{-5, one}, {1000, two}}}},
        m::Insn{m::Goto{end}},
        one,
        m::Insn{m::Goto{end}},
        two,
        m::Insn{m::Goto{end}},
        end,
        m::Insn{m::ReturnVoid{}},
    };
    // A name of its own, so builds running their tests at the same time do
    // not share the file.
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        std::format("dexpp_disassembly_{}.dex", std::random_device{}());
    auto written = m::write_file(dex, path.string());
    ASSERT_TRUE(written.has_value()) << written.error().message;
    auto ctx = AnalysisContext::from_dex(path.string());
    std::filesystem::remove(path);
    ASSERT_TRUE(ctx.has_value()) << ctx.error().message;

    std::vector<DisassemblyLine> lines = method_named(*ctx, "LDis;", "f").disassemble();
    std::vector<std::string> text;
    for (const DisassemblyLine &l : lines)
        text.push_back(std::format("{:04x} {}", l.offset, l.text));
    EXPECT_EQ(text, (std::vector<std::string>{
                        "0000 const-string v0, \"line\\u2028sep \\\"q\\\"\"",
                        "0002 invoke-custom {v0}, call_site@0(\"go\", ()V, -0x2, 0x10L, 0x1t, "
                        "-0x1s, '\\'', 1.5f, -0.25, true, null, \"a\\tb\", [I, (J)Z, "
                        "static-get@LDis;->x:I, LE;->A:LE;, {0x1, 0x2})@LDis;->boot("
                        "Ljava/lang/invoke/MethodHandles$Lookup;Ljava/lang/String;"
                        "Ljava/lang/invoke/MethodType;[Ljava/lang/Object;)"
                        "Ljava/lang/invoke/CallSite;",
                        "0005 fill-array-data v0, [0x1, -0x1]",
                        "0008 sparse-switch v1, {-0x5 -> 000c, 0x3e8 -> 000d}", "000b goto 000e",
                        "000c goto 000e", "000d goto 000e", "000e return-void",
                        "000f nop", // aligns the payloads that follow
                    }));
}

TEST(DisassembleDetailTest, StringsAreEscaped)
{
    // const-string v0, string@1; const-string/jumbo v1, string@2; const-string v2, string@3
    EXPECT_EQ(
        render({0x001a, 0x0001, 0x011b, 0x0002, 0x0000, 0x021a, 0x0003}),
        (std::vector<std::string>{
            "const-string v0, \"tab\\there \\\"quoted\\\" back\\\\slash\\u0001 \\u007f end\\n\"",
            "const-string/jumbo v1, \"caf\xc3\xa9 \\u202eevil \\u200b zero \\ud800 lone\"",
            "const-string v2, \"bad \\ufffd utf8 \\ufffd\"",
        }));
    EXPECT_EQ(detail::escape_text("a\"b", false), "a\"b");
    EXPECT_EQ(detail::escape_text("a\"b", true), "\"a\\\"b\"");
    EXPECT_EQ(detail::escape_text("\xf0\x9f\x98\x80", true), "\"\xf0\x9f\x98\x80\"")
        << "visible characters outside the BMP stay as they are";
}

TEST(DisassembleDetailTest, IndicesOutsideTheirTableAreMarked)
{
    EXPECT_EQ(render({
                  0x001a, 0x000c,                 // const-string v0, string@12
                  0x001c, 0x000b,                 // const-class v0, type@11
                  0x0060, 0x0063,                 // sget v0, field@99
                  0x1052, 0x0064,                 // iget v0, v1, field@100
                  0x1071, 0x00c8, 0x0000,         // invoke-static {v0}, method@200
                  0x10fa, 0x0007, 0x0000, 0x000a, // invoke-polymorphic {v0}, method@7, proto@10
                  0x00fe, 0x000d,                 // const-method-handle v0, method_handle@13
                  0x00ff, 0x000e,                 // const-method-type v0, proto@14
                  0x10fc, 0x000f, 0x0000,         // invoke-custom {v0}, call_site@15
              }),
              (std::vector<std::string>{
                  "const-string v0, string@12?",
                  "const-class v0, type@11?",
                  "sget v0, field@99?",
                  "iget v0, v1, field@100?",
                  "invoke-static {v0}, method@200?",
                  "invoke-polymorphic {v0}, LC;->m()V7, proto@10?",
                  "const-method-handle v0, method_handle@13?",
                  "const-method-type v0, proto@14?",
                  "invoke-custom {v0}, call_site@15?",
              }));
    // In range, every one of them is named.
    EXPECT_EQ(render({0x10fc, 0x0002, 0x0000, 0x00fe, 0x0001, 0x00ff, 0x0000}),
              (std::vector<std::string>{
                  "invoke-custom {v0}, call_site@2(\"run\", ()V)@LC;->b()V2",
                  "const-method-handle v0, invoke-static@LC;->m()V1",
                  "const-method-type v0, (I)V",
              }));
}

TEST(DisassembleDetailTest, RegistersLiteralsAndBranches)
{
    EXPECT_EQ(render({
                  0xf012,                 // 0: const/4 v0, -1
                  0x0116, 0xff80,         // 1: const-wide/16 v1, -0x80
                  0x0215, 0x3f80,         // 3: const/high16 v2, 0x3f800000
                  0x10b0,                 // 5: add-int/2addr v0, v1
                  0x0090, 0x0201,         // 6: add-int v0, v1, v2
                  0x00d8, 0xfe01,         // 8: add-int/lit8 v0, v1, -2
                  0x0374, 0x0005, 0x0004, // a: invoke-virtual/range {v4 .. v6}, method@5
                  0x0074, 0x0005, 0x0000, // d: invoke-virtual/range {}, method@5
                  0x2024, 0x0001, 0x0010, // 10: filled-new-array {v0, v1}, type@1
                  0x0225, 0x0001, 0x0003, // 13: filled-new-array/range {v3 .. v4}, type@1
                  0x0038, 0xffee,         // 16: if-eqz v0, -0x12 -> 0004
                  0x1032, 0x0010,         // 18: if-eq v0, v1, +0x10 -> 0028? (past the end)
                  0x2828,                 // 1a: goto +0x28 -> 0042?
                  0xe628,                 // 1b: goto -0x1a -> 0001
                  0xd028,                 // 1c: goto -0x30 -> -0014?
                  0x000e,                 // 1d: return-void
              }),
              (std::vector<std::string>{
                  "const/4 v0, -0x1",
                  "const-wide/16 v1, -0x80L",
                  "const/high16 v2, 0x3f800000",
                  "add-int/2addr v0, v1",
                  "add-int v0, v1, v2",
                  "add-int/lit8 v0, v1, -0x2",
                  "invoke-virtual/range {v4 .. v6}, LC;->m()V5",
                  "invoke-virtual/range {}, LC;->m()V5",
                  "filled-new-array {v0, v1}, LT;1",
                  "filled-new-array/range {v3 .. v4}, LT;1",
                  "if-eqz v0, 0004",
                  "if-eq v0, v1, 0028?",
                  "goto 0042?",
                  "goto 0001",
                  "goto -0014?",
                  "return-void",
              }));
}

TEST(DisassembleDetailTest, PayloadsAreWrittenOut)
{
    const std::vector<uint16_t> code = {
        0x002c,
        0x000c,
        0x0000, // 00: sparse-switch v0, +0xc -> 000c
        0x0026,
        0x0013,
        0x0000, // 03: fill-array-data v0, +0x13 -> 0016 (bytes)
        0x0026,
        0x0016,
        0x0000, // 06: fill-array-data v0, +0x16 -> 001c (shorts)
        0x0026,
        0x0019,
        0x0000, // 09: fill-array-data v0, +0x19 -> 0022 (longs)
        // 0c: sparse-switch payload, 2 cases: keys -1 and 0x10000, targets +3 and +6
        0x0200,
        0x0002,
        0xffff,
        0xffff,
        0x0000,
        0x0001,
        0x0003,
        0x0000,
        0x0006,
        0x0000,
        // 16: 3 elements of 1 byte: 0x7f, 0x80, 0x01
        0x0300,
        0x0001,
        0x0003,
        0x0000,
        0x807f,
        0x0001,
        // 1c: 2 elements of 2 bytes: 0x1234, 0xffff
        0x0300,
        0x0002,
        0x0002,
        0x0000,
        0x1234,
        0xffff,
        // 22: 1 element of 8 bytes: -2
        0x0300,
        0x0008,
        0x0001,
        0x0000,
        0xfffe,
        0xffff,
        0xffff,
        0xffff,
    };
    std::vector<std::string> text = render(code);
    ASSERT_EQ(text.size(), 4u) << "the payloads are not instructions";
    EXPECT_EQ(text[0], "sparse-switch v0, {-0x1 -> 0003, 0x10000 -> 0006}");
    EXPECT_EQ(text[1], "fill-array-data v0, [0x7ft, -0x80t, 0x1t]");
    EXPECT_EQ(text[2], "fill-array-data v0, [0x1234s, -0x1s]");
    EXPECT_EQ(text[3], "fill-array-data v0, [-0x2L]");
}

TEST(DisassembleDetailTest, DamagedPayloadsAreMarked)
{
    EXPECT_EQ(render({
                  0x002b, 0x0010, 0x0000, // 0: packed-switch v0, +0x10, on no payload
                  0x002b, 0x0006, 0x0000, // 3: packed-switch v0, +6 -> 0009, wrong ident
                  0x0026, 0x0005, 0x0000, // 6: fill-array-data v0, +5 -> 000b, width 3
                  0x0200, 0x0000,         // 9: (an empty sparse-switch payload)
                  0x0300, 0x0003, 0x0001, 0x0000, 0x0000, 0x0000, // b: width-3 array
                  0x002c, 0xfff8, 0xffff, // 11: sparse-switch v0, -8 -> 0009, empty
                  0x0026, 0xffe0, 0xffff, // 14: fill-array-data v0, -0x20 -> -000c
              }),
              (std::vector<std::string>{
                  "packed-switch v0, 0010?",
                  "packed-switch v0, 0009?",
                  "fill-array-data v0, 000b?",
                  "sparse-switch v0, {}",
                  "fill-array-data v0, -000c?",
              }));
}

TEST(DisassembleDetailTest, MethodHandleKindNames)
{
    EXPECT_EQ(detail::method_handle_kind_name(0), "static-put");
    EXPECT_EQ(detail::method_handle_kind_name(4), "invoke-static");
    EXPECT_EQ(detail::method_handle_kind_name(8), "invoke-interface");
    EXPECT_EQ(detail::method_handle_kind_name(9), "");
}
