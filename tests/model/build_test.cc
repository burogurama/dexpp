// Building models from scratch: the encodings write() picks, branches that
// grow, switches, try ranges, debug info, values, strings and call sites.

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "../raw/test_utils.hpp"
#include "describe.hpp"
#include "dex.hpp"
#include "dex_check.hpp"
#include "model/model.hpp"
#include "raw/opcodes.hpp"
#include "raw/parser.hpp"

using namespace dex::model;
using dex::AccessFlags;
using dex::FieldId;
using dex::MethodId;

namespace {

/** One public class "LT;" with one public static method m()V holding @p items. */
Dex with_code(std::vector<Item> items, Reg registers, std::vector<TryRange> tries = {})
{
    Dex dex;
    ClassDef &c = dex.classes.emplace_back();
    c.type = "LT;";
    c.access = AccessFlags::Public;
    c.superclass = "Ljava/lang/Object;";
    MethodDef &m = c.methods.emplace_back();
    m.name = "m";
    m.proto = "()V";
    m.access = AccessFlags::Public | AccessFlags::Static;
    Code &code = m.code.emplace();
    code.registers = registers;
    code.items = std::move(items);
    code.tries = std::move(tries);
    return dex;
}

std::vector<uint8_t> write_ok(const Dex &dex, const WriteOptions &options = {})
{
    auto out = write(dex, options);
    if (!out) {
        ADD_FAILURE() << out.error().message;
        for (const Problem &p : out.error().problems)
            ADD_FAILURE() << p.where << ": " << p.message;
        return {};
    }
    for (const std::string &p : check_dex(*out))
        ADD_FAILURE() << p;
    return std::move(*out);
}

/** The code item written for method @p name of class @p type. */
dex::raw::CodeItem written_code(std::span<const uint8_t> bytes, std::string_view type,
                                std::string_view name)
{
    using namespace dex::raw;
    auto file = parser::parse_buffer(bytes);
    EXPECT_TRUE(file.has_value());
    auto strings = file->strings();
    for (const ClassDefItem &cd : file->class_defs()) {
        if (strings[file->type_ids()[cd.class_idx]] != type)
            continue;
        auto data = parser::parse_class_data(bytes, cd.class_data_off);
        for (const auto *list : {&data->direct_methods, &data->virtual_methods})
            for (const EncodedMethod &em : *list)
                if (strings[file->method_ids()[em.method_idx].name_idx] == name)
                    return *parser::parse_code_item(bytes, em.code_off);
    }
    ADD_FAILURE() << "no method " << type << "->" << name;
    return {};
}

/** The opcodes of the instructions in @p insns, payloads skipped. */
std::vector<uint8_t> opcodes(const std::vector<uint16_t> &insns)
{
    std::vector<uint8_t> out;
    for (uint32_t pos = 0; pos < insns.size();) {
        if (auto payload = dex::raw::payload_units(insns, pos)) {
            pos += static_cast<uint32_t>(*payload);
            continue;
        }
        auto f = dex::raw::decode_insn(insns, pos);
        if (!f) {
            ADD_FAILURE() << "bad instruction at " << pos;
            break;
        }
        out.push_back(f->opcode);
        pos += dex::raw::opcode_info(f->opcode).units;
    }
    return out;
}

Dex reread(std::span<const uint8_t> bytes)
{
    auto dex = read(bytes);
    EXPECT_TRUE(dex.has_value()) << (dex ? "" : dex.error().message);
    return dex ? std::move(*dex) : Dex{};
}

} // namespace

TEST(ModelBuild, HelloWorld)
{
    Dex dex;
    ClassDef &cls = dex.classes.emplace_back();
    cls.type = "LHello;";
    cls.access = AccessFlags::Public;
    cls.superclass = "Ljava/lang/Object;";
    cls.source_file = "Hello.java";
    MethodDef &main = cls.methods.emplace_back();
    main.name = "main";
    main.proto = "([Ljava/lang/String;)V";
    main.access = AccessFlags::Public | AccessFlags::Static;
    Code &code = main.code.emplace();
    code.registers = 3;
    code.items = {
        Insn{StaticGet{ValueKind::Object, 0,
                       FieldId{"Ljava/lang/System;", "out", "Ljava/io/PrintStream;"}}},
        Insn{ConstString{1, "Hello"}},
        Insn{Invoke{Invoke::Kind::Virtual,
                    {0, 1},
                    MethodId{"Ljava/io/PrintStream;", "println", "(Ljava/lang/String;)V"}}},
        Insn{ReturnVoid{}},
    };

    auto bytes = write_ok(dex);
    EXPECT_EQ(reread(bytes), dex);

    auto ci = written_code(bytes, "LHello;", "main");
    EXPECT_EQ(ci.registers_size, 3);
    EXPECT_EQ(ci.ins_size, 1);  // static: just the String[]
    EXPECT_EQ(ci.outs_size, 2); // println(this, s)

    std::string path = temp_file("hello", bytes);
    auto ctx = dex::AnalysisContext::from_dex(path);
    std::filesystem::remove(path);
    ASSERT_TRUE(ctx.has_value());
    auto hello = ctx->find_class("LHello;");
    ASSERT_TRUE(hello.has_value());
    auto methods = hello->methods();
    ASSERT_EQ(methods.size(), 1u);
    auto calls = methods[0].calls();
    ASSERT_EQ(calls.size(), 1u);
    EXPECT_EQ(calls[0].target.name, "println");
    auto strings = methods[0].string_loads();
    ASSERT_EQ(strings.size(), 1u);
    EXPECT_EQ(strings[0].value, "Hello");
}

TEST(ModelBuild, PicksTheSmallestConstEncoding)
{
    auto dex =
        with_code({Insn{Const{0, 5}}, Insn{Const{0, -8}}, Insn{Const{0, 100}}, Insn{Const{20, 5}},
                   Insn{Const{0, 0x10000}}, Insn{Const{0, 0x12345}}, Insn{ConstWide{0, 7}},
                   Insn{ConstWide{0, int64_t{1} << 48}}, Insn{ConstWide{0, 0x12345678}},
                   Insn{ConstWide{0, 0x123456789}}, Insn{ReturnVoid{}}},
                  22);
    auto bytes = write_ok(dex);
    EXPECT_EQ(
        opcodes(written_code(bytes, "LT;", "m").insns),
        (std::vector<uint8_t>{0x12, 0x12, 0x13, 0x13, 0x15, 0x14, 0x16, 0x19, 0x17, 0x18, 0x0e}));
    EXPECT_EQ(reread(bytes), dex);
}

TEST(ModelBuild, PicksMoveAndBinaryForms)
{
    auto dex =
        with_code({Insn{Move{ValueKind::Normal, 1, 2}}, Insn{Move{ValueKind::Normal, 1, 300}},
                   Insn{Move{ValueKind::Normal, 300, 1}},
                   Insn{Binary{BinaryOp::AddInt, 1, 1, 2}},   // 2-address form
                   Insn{Binary{BinaryOp::AddInt, 1, 2, 3}},   // three registers
                   Insn{Binary{BinaryOp::AddInt, 20, 20, 2}}, // v20: no 2-address form
                   Insn{BinaryLiteral{LiteralOp::Add, 1, 2, 5}},
                   Insn{BinaryLiteral{LiteralOp::Add, 1, 2, 1000}}, Insn{ReturnVoid{}}},
                  301);
    auto bytes = write_ok(dex);
    EXPECT_EQ(opcodes(written_code(bytes, "LT;", "m").insns),
              (std::vector<uint8_t>{0x01, 0x02, 0x03, 0xb0, 0x90, 0x90, 0xd8, 0xd0, 0x0e}));
    EXPECT_EQ(reread(bytes), dex);
}

TEST(ModelBuild, GotoToItselfUsesGoto32)
{
    Dex dex = with_code({}, 1);
    Code &code = *dex.classes[0].methods[0].code;
    Label loop = code.new_label();
    code.items = {loop, Insn{Goto{loop}}};
    auto bytes = write_ok(dex);
    EXPECT_EQ(written_code(bytes, "LT;", "m").insns, (std::vector<uint16_t>{0x002a, 0, 0}));
    EXPECT_EQ(reread(bytes), dex);
}

TEST(ModelBuild, IfToItselfBranchesBackToANop)
{
    Dex dex = with_code({}, 1);
    Code &code = *dex.classes[0].methods[0].code;
    Label spin = code.new_label();
    code.items = {spin, Insn{IfZero{Condition::Eq, 0, spin}}, Insn{ReturnVoid{}}};
    auto bytes = write_ok(dex);
    // nop; if-eqz v0, -1; return-void
    EXPECT_EQ(written_code(bytes, "LT;", "m").insns,
              (std::vector<uint16_t>{0x0000, 0x0038, 0xffff, 0x000e}));
}

TEST(ModelBuild, FarBranchesGrow)
{
    Dex dex = with_code({}, 1);
    Code &code = *dex.classes[0].methods[0].code;
    Label mid = code.new_label();
    Label far = code.new_label();
    code.items.push_back(Insn{IfZero{Condition::Eq, 0, far}}); // 40000 units away
    code.items.push_back(Insn{Goto{mid}});                     // 200 units away
    for (int i = 0; i < 200; ++i)
        code.items.push_back(Insn{Nop{}});
    code.items.push_back(mid);
    for (int i = 0; i < 40000; ++i)
        code.items.push_back(Insn{Nop{}});
    code.items.push_back(far);
    code.items.push_back(Insn{ReturnVoid{}});

    auto bytes = write_ok(dex);
    auto insns = written_code(bytes, "LT;", "m").insns;
    // if-nez v0, +5 over a goto/32, then goto/16 to mid.
    auto first = dex::raw::decode_insn(insns, 0);
    EXPECT_EQ(first->opcode, 0x39); // if-nez: the condition turned around
    EXPECT_EQ(first->offset, 5);
    auto jump = dex::raw::decode_insn(insns, 2);
    EXPECT_EQ(jump->opcode, 0x2a);
    EXPECT_EQ(jump->offset, 5 + 2 + 200 + 40000 - 2); // to `far`, from the goto/32 at 2
    auto near = dex::raw::decode_insn(insns, 5);
    EXPECT_EQ(near->opcode, 0x29);
    EXPECT_EQ(near->offset, 2 + 200);
    // Reading back gives the same program, with the far branch spelled out.
    Dex back = reread(bytes);
    const auto &items = back.classes[0].methods[0].code->items;
    ASSERT_TRUE(std::holds_alternative<Insn>(items[0]));
    EXPECT_TRUE(std::holds_alternative<IfZero>(std::get<Insn>(items[0])));
    auto again = write(back);
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(*again, bytes);
}

TEST(ModelBuild, DenseSwitchIsPackedSparseOtherwise)
{
    for (bool dense : {true, false}) {
        Dex dex = with_code({}, 1);
        Code &code = *dex.classes[0].methods[0].code;
        Label a = code.new_label(), b = code.new_label();
        std::vector<Switch::Case> cases =
            dense ? std::vector<Switch::Case>{{3, a}, {1, b}, {2, a}}
                  : std::vector<Switch::Case>{{1, a}, {1000, b}, {-7, a}};
        code.items = {Insn{Switch{0, cases}}, Insn{ReturnVoid{}}, a, Insn{ReturnVoid{}}, b,
                      Insn{ReturnVoid{}}};
        auto bytes = write_ok(dex);
        auto insns = written_code(bytes, "LT;", "m").insns;
        EXPECT_EQ(insns[0] & 0xFF, dense ? 0x2b : 0x2c);
        // Cases come back sorted by key.
        Dex back = reread(bytes);
        auto &sw = std::get<Switch>(std::get<Insn>(back.classes[0].methods[0].code->items[0]));
        std::sort(cases.begin(), cases.end(), [](auto &x, auto &y) { return x.key < y.key; });
        ASSERT_EQ(sw.cases.size(), 3u);
        for (std::size_t i = 0; i < 3; ++i)
            EXPECT_EQ(sw.cases[i].key, cases[i].key);
    }
}

TEST(ModelBuild, PackedSwitchGapsFallThrough)
{
    Dex dex = with_code({}, 1);
    Code &code = *dex.classes[0].methods[0].code;
    Label a = code.new_label();
    code.items = {Insn{Switch{0, {{1, a}, {3, a}}}}, Insn{ReturnVoid{}}, a, Insn{ReturnVoid{}}};
    auto bytes = write_ok(dex);
    EXPECT_EQ(written_code(bytes, "LT;", "m").insns[0] & 0xFF, 0x2b); // packed, key 2 is a gap
    EXPECT_EQ(reread(bytes), dex); // the gap reads back as no case
}

TEST(ModelBuild, FillArrayDataRoundTrips)
{
    Dex dex =
        with_code({Insn{Const{0, 3}}, Insn{NewArray{1, 0, "[B"}},
                   Insn{FillArrayData{1, 1, {1, 2, 3}}}, // odd byte count
                   Insn{NewArray{2, 0, "[J"}},
                   Insn{FillArrayData{2, 8, std::vector<uint8_t>(24, 0xAB)}}, Insn{ReturnVoid{}}},
                  3);
    auto bytes = write_ok(dex);
    EXPECT_EQ(reread(bytes), dex);
}

TEST(ModelBuild, NestedTryRangesAreFlattened)
{
    Dex dex = with_code({}, 2);
    Code &code = *dex.classes[0].methods[0].code;
    Label l0 = code.new_label(), l1 = code.new_label(), l2 = code.new_label(),
          l3 = code.new_label(), h_outer = code.new_label(), h_inner = code.new_label();
    MethodId call{"LT;", "m", "()V"};
    code.items = {l0,
                  Insn{Invoke{Invoke::Kind::Static, {}, call}},
                  l1,
                  Insn{Invoke{Invoke::Kind::Static, {}, call}},
                  l2,
                  Insn{Invoke{Invoke::Kind::Static, {}, call}},
                  l3,
                  Insn{ReturnVoid{}},
                  h_outer,
                  Insn{MoveException{0}},
                  Insn{ReturnVoid{}},
                  h_inner,
                  Insn{MoveException{0}},
                  Insn{ReturnVoid{}}};
    code.tries = {
        TryRange{
            l1, l2, {Catch{"Ljava/io/IOException;", h_inner}}, std::nullopt}, // listed first: wins
        TryRange{l0, l3, {Catch{"Ljava/lang/Exception;", h_outer}}, std::nullopt},
    };
    auto bytes = write_ok(dex);
    auto ci = written_code(bytes, "LT;", "m");
    ASSERT_EQ(ci.tries.size(), 3u);
    EXPECT_EQ(ci.tries[0].start_addr, 0u);
    EXPECT_EQ(ci.tries[1].start_addr, 3u);
    EXPECT_EQ(ci.tries[2].start_addr, 6u);
    // The middle piece is covered by both ranges: IOException first.
    Dex back = reread(bytes);
    const auto &tries = back.classes[0].methods[0].code->tries;
    ASSERT_EQ(tries.size(), 3u);
    ASSERT_EQ(tries[1].catches.size(), 2u);
    EXPECT_EQ(tries[1].catches[0].type, "Ljava/io/IOException;");
    EXPECT_EQ(tries[1].catches[1].type, "Ljava/lang/Exception;");
    EXPECT_EQ(tries[0].catches.size(), 1u);
}

TEST(ModelBuild, DebugInfoRoundTrips)
{
    Dex dex = with_code({}, 2);
    MethodDef &m = dex.classes[0].methods[0];
    m.proto = "(I)V";
    Code &code = *m.code;
    code.parameter_names = {std::string("count")};
    code.items = {
        LineNumber{10},
        PrologueEnd{},
        LineNumber{11},
        Insn{Const{0, 1}},
        LocalStart{0, LocalVariable{"x", "I", std::nullopt}},
        LineNumber{200}, // a big jump: DBG_ADVANCE_LINE
        Insn{Const{0, 2}},
        LocalEnd{0},
        SetSourceFile{"Other.java"},
        LineNumber{3},
        LocalRestart{0},
        EpilogueBegin{},
        LineNumber{4},
        Insn{ReturnVoid{}},
        LocalStart{0, LocalVariable{"list", "Ljava/util/List;", "Ljava/util/List<TT;>;"}}};
    auto bytes = write_ok(dex);
    EXPECT_EQ(reread(bytes), dex);

    // And stripped on request.
    auto stripped = write_ok(dex, WriteOptions{.debug_info = false});
    EXPECT_EQ(written_code(stripped, "LT;", "m").debug_info_off, 0u);
    Dex plain = reread(stripped);
    const Code &c = *plain.classes[0].methods[0].code;
    EXPECT_TRUE(c.parameter_names.empty());
    for (const Item &item : c.items)
        EXPECT_TRUE(std::holds_alternative<Insn>(item) || std::holds_alternative<Label>(item));
}

TEST(ModelBuild, ValuesKeepTheirExactType)
{
    Dex dex;
    dex.version = 38; // method handle and method type values
    ClassDef &c = dex.classes.emplace_back();
    c.type = "LV;";
    c.superclass = "Ljava/lang/Object;";
    auto field = [&](std::string name, std::string type, Value v) {
        FieldDef &f = c.fields.emplace_back();
        f.name = std::move(name);
        f.type = std::move(type);
        f.access = AccessFlags::Static | AccessFlags::Final;
        f.initial_value = std::move(v);
    };
    // In name order: the order write() puts them in, so the model reads back equal.
    field("b", "B", Value{Value::Byte{-128}});
    field("c", "C", Value{Value::Char{u'\xe9'}});
    field("cls", "Ljava/lang/Class;", Value{Value::Type{"[I"}});
    field("d", "D", Value{Value::Double{1.5}});
    field("f", "F", Value{Value::Float{-0.0f}});
    field("i", "I", Value{Value::Int{std::numeric_limits<int32_t>::min()}});
    field("j", "J", Value{Value::Long{0x123456789ABCDEF0}});
    field("nan", "F", Value{Value::Float{std::bit_cast<float>(0x7F800001u)}}); // signaling NaN
    field("nul", "Ljava/lang/Object;", Value{Value::Null{}});
    field("s", "S", Value{Value::Short{-300}});
    field("str", "Ljava/lang/String;", Value{Value::String{"text"}});
    field("z", "Z", Value{Value::Boolean{true}});

    EncodedAnnotation inner{"LInner;", {NamedValue{"value", Value{Value::Int{1}}}}};
    AnnotationItem everything;
    everything.visibility = dex::AnnotationVisibility::Runtime;
    everything.annotation.type = "LAll;";
    everything.annotation.elements = {
        {"array", Value{Value::Array{{Value{Value::Int{1}}, Value{Value::String{"a"}}}}}},
        {"charValue", Value{Value::Char{u'a'}}}, // stays a char, not an int
        {"enumValue", Value{Value::Enum{FieldId{"LE;", "ONE", "LE;"}}}},
        {"fieldValue", Value{Value::Field{FieldId{"LV;", "i", "I"}}}},
        {"handle",
         Value{MethodHandle{MethodHandleKind::InvokeStatic, MethodId{"LV;", "m", "()V"}}}},
        {"methodType", Value{Value::MethodType{"(I)V"}}},
        {"methodValue", Value{Value::Method{MethodId{"LV;", "m", "()V"}}}},
        {"nested", Value{Shared<EncodedAnnotation>(inner)}},
    };
    c.annotations.push_back(everything);

    auto bytes = write_ok(dex);
    Dex back = reread(bytes);
    EXPECT_EQ(back, dex);
    // -0.0 and 0.0 are different values; a signaling NaN keeps its bits.
    const FieldDef *f = back.classes[0].find_field("f", "F");
    EXPECT_TRUE(std::signbit(std::get<Value::Float>(f->initial_value->data).value));
    const FieldDef *nan = back.classes[0].find_field("nan", "F");
    EXPECT_EQ(std::bit_cast<uint32_t>(std::get<Value::Float>(nan->initial_value->data).value),
              0x7F800001u);
    EXPECT_FALSE(Value{Value::Float{0.0f}} == Value{Value::Float{-0.0f}});

    // Members and annotation elements in another order come back sorted.
    std::reverse(c.fields.begin(), c.fields.end());
    std::reverse(c.annotations[0].annotation.elements.begin(),
                 c.annotations[0].annotation.elements.end());
    EXPECT_EQ(reread(write_ok(dex)), back);
}

TEST(ModelBuild, StringsKeepUnpairedSurrogates)
{
    const std::string lone = "a\xED\xA0\x80";             // an unpaired high surrogate
    const std::string split = "\xED\xA0\xBD\xED\xB8\x80"; // U+1F600 as two 3-byte sequences
    const std::string joined = "\xF0\x9F\x98\x80";        // U+1F600 as one 4-byte sequence
    auto dex = with_code({Insn{ConstString{0, lone}}, Insn{ConstString{0, split}},
                          Insn{ConstString{0, joined}}, Insn{ReturnVoid{}}},
                         1);
    auto bytes = write_ok(dex);
    EXPECT_EQ(pool_stats(dex).strings, pool_stats(reread(bytes)).strings);
    Dex back = reread(bytes);
    const auto &items = back.classes[0].methods[0].code->items;
    EXPECT_EQ(std::get<ConstString>(std::get<Insn>(items[0])).value.get(), lone);
    // The two spellings of U+1F600 are one DEX string; it reads back joined.
    EXPECT_EQ(std::get<ConstString>(std::get<Insn>(items[1])).value.get(), joined);
    EXPECT_EQ(std::get<ConstString>(std::get<Insn>(items[2])).value.get(), joined);
    auto file = dex::raw::parser::parse_buffer(bytes);
    std::size_t count = 0;
    for (const std::string &s : file->strings())
        count += s.starts_with("\xF0\x9F") ? 1 : 0;
    EXPECT_EQ(count, 1u);
}

TEST(ModelBuild, CallSitesShareByKey)
{
    auto site = [](uint32_t key) {
        CallSite cs;
        cs.bootstrap = MethodHandle{MethodHandleKind::InvokeStatic,
                                    MethodId{"LBoot;", "bootstrap",
                                             "(Ljava/lang/invoke/MethodHandles$Lookup;Ljava/lang/"
                                             "String;Ljava/lang/invoke/MethodType;)Ljava/lang/"
                                             "invoke/CallSite;"}};
        cs.method_name = "run";
        cs.method_type = "()V";
        cs.share_key = key;
        return cs;
    };
    Shared<CallSite> shared = site(7);
    Dex dex = with_code({Insn{InvokeCustom{{}, shared}}, Insn{InvokeCustom{{}, shared}},
                         Insn{InvokeCustom{{}, site(0)}}, Insn{InvokeCustom{{}, site(0)}},
                         Insn{ReturnVoid{}}},
                        1);
    dex.version = 38;
    EXPECT_EQ(pool_stats(dex).call_sites, 3u); // key 7 once, key 0 twice
    auto bytes = write_ok(dex);
    auto file = dex::raw::parser::parse_buffer(bytes);
    EXPECT_EQ(file->call_site_ids().size(), 3u);
    // Reading keeps the sharing: the re-read model writes the same call sites.
    EXPECT_EQ(pool_stats(reread(bytes)).call_sites, 3u);

    dex.version = 37;
    auto low = write(dex);
    ASSERT_FALSE(low.has_value());
    EXPECT_EQ(low.error().kind, Error::Kind::VersionTooLow);
}

TEST(ModelBuild, SupertypesAreWrittenFirst)
{
    Dex dex;
    auto add = [&](std::string type, std::string super, std::vector<std::string> interfaces) {
        ClassDef &c = dex.classes.emplace_back();
        c.type = std::move(type);
        c.superclass = std::move(super);
        c.interfaces = std::move(interfaces);
    };
    add("LC;", "LB;", {"LI;"});
    add("LB;", "LA;", {});
    add("LI;", "Ljava/lang/Object;", {});
    dex.classes[2].access = AccessFlags::Interface | AccessFlags::Abstract;
    add("LA;", "Ljava/lang/Object;", {});
    auto bytes = write_ok(dex);
    std::vector<std::string> order;
    for (const ClassDef &c : reread(bytes).classes)
        order.push_back(c.type);
    EXPECT_EQ(order, (std::vector<std::string>{"LA;", "LB;", "LI;", "LC;"}));
}

TEST(ModelBuild, InitWithoutTheConstructorFlagIsDirect)
{
    Dex dex = with_code({Insn{ReturnVoid{}}}, 1);
    MethodDef &m = dex.classes[0].methods[0];
    m.name = "<init>";
    m.access = AccessFlags::Public; // no Constructor flag: still a direct method
    auto bytes = write_ok(dex);     // check_dex verifies the direct/virtual split
    auto file = dex::raw::parser::parse_buffer(bytes);
    auto data = dex::raw::parser::parse_class_data(bytes, file->class_defs()[0].class_data_off);
    EXPECT_EQ(data->direct_methods.size(), 1u);
    EXPECT_EQ(data->virtual_methods.size(), 0u);
}

TEST(ModelBuild, StaticValueGapsGetDefaults)
{
    Dex dex;
    ClassDef &c = dex.classes.emplace_back();
    c.type = "LS;";
    c.superclass = "Ljava/lang/Object;";
    for (const char *name : {"a", "b", "c"}) {
        FieldDef &f = c.fields.emplace_back();
        f.name = name;
        f.type = "I";
        f.access = AccessFlags::Static;
    }
    c.fields[1].initial_value = Value{Value::Int{5}};
    Dex back = reread(write_ok(dex));
    const ClassDef &r = back.classes[0];
    EXPECT_EQ(r.find_field("a", "I")->initial_value, Value{Value::Int{0}}); // filled in
    EXPECT_EQ(r.find_field("b", "I")->initial_value, Value{Value::Int{5}});
    EXPECT_FALSE(r.find_field("c", "I")->initial_value.has_value()); // trailing: left out
}

TEST(ModelBuild, OutputIsDeterministic)
{
    auto input = load_buf("tests/data/inherit.dex");
    auto dex = read(input);
    ASSERT_TRUE(dex.has_value());
    Dex copy = *dex;
    EXPECT_EQ(write_ok(*dex), write_ok(copy));
}

TEST(ModelRegList, InlineAndHeap)
{
    RegList small{1, 2, 3};
    EXPECT_EQ(small.size(), 3u);
    EXPECT_TRUE(small.is_run());
    RegList big = RegList::range(10, 300);
    EXPECT_EQ(big.size(), 300u);
    EXPECT_TRUE(big.is_run());
    EXPECT_EQ(big[299], 309);
    RegList copy = big;
    EXPECT_EQ(copy, big);
    RegList moved = std::move(copy);
    EXPECT_EQ(moved, big);
    EXPECT_TRUE(copy.empty());
    moved = small;
    EXPECT_EQ(moved, small);
    RegList gaps{1, 3};
    EXPECT_FALSE(gaps.is_run());
}

TEST(ModelBuild, WritingIsAFixpointWhateverTheModelOrder)
{
    // Members and annotations listed out of the file's order: the output
    // must not depend on that order, so writing the re-read model gives the
    // same bytes.
    Dex dex;
    ClassDef &c = dex.classes.emplace_back();
    c.type = "LT;";
    c.superclass = "Ljava/lang/Object;";
    auto annotation = [](std::string type) {
        AnnotationItem a;
        a.annotation.type = std::move(type);
        return a;
    };
    c.annotations = {annotation("LZ;"), annotation("LA;"), annotation("LM;")};
    for (const char *name : {"z", "b", "a"}) {
        FieldDef &f = c.fields.emplace_back();
        f.name = name;
        f.type = "I";
        f.annotations = {annotation(std::string("LF") + name + ";"), annotation("LA;")};
    }
    auto first = write_ok(dex);
    Dex back = reread(first);
    EXPECT_NE(back, dex); // sorted on the way back
    EXPECT_EQ(write_ok(back), first);
}
