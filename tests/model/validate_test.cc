// validate(), required_version(), and the errors write() reports.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "model/model.hpp"
#include "raw/parser.hpp"

using namespace dex::model;
using dex::AccessFlags;
using dex::FieldId;
using dex::MethodId;

namespace {

/** A valid model: class "LT;" with static method m()V returning. */
Dex valid()
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
    code.registers = 4;
    code.items = {Insn{ReturnVoid{}}};
    return dex;
}

ClassDef &cls(Dex &dex) { return dex.classes[0]; }
MethodDef &method(Dex &dex) { return dex.classes[0].methods[0]; }
Code &code(Dex &dex) { return *dex.classes[0].methods[0].code; }

/** True if validate() reports a problem whose message contains @p text. */
bool reports(const Dex &dex, std::string_view text)
{
    for (const Problem &p : validate(dex))
        if (p.message.find(text) != std::string::npos)
            return true;
    return false;
}

} // namespace

TEST(ModelValidate, ValidModelHasNoProblems)
{
    EXPECT_TRUE(validate(valid()).empty());
    EXPECT_EQ(required_version(valid()), 35u);
}

TEST(ModelValidate, Classes)
{
    Dex dex = valid();
    dex.classes.push_back(cls(dex));
    EXPECT_TRUE(reports(dex, "defined twice"));

    dex = valid();
    cls(dex).type = "T";
    EXPECT_TRUE(reports(dex, "invalid class descriptor"));

    dex = valid();
    cls(dex).superclass.reset();
    EXPECT_TRUE(reports(dex, "only java.lang.Object may have no superclass"));

    dex = valid();
    cls(dex).interfaces = {"LI;", "LI;"};
    EXPECT_TRUE(reports(dex, "listed twice"));

    dex = valid();
    cls(dex).access = static_cast<AccessFlags>(0x10000);
    EXPECT_TRUE(reports(dex, "invalid class access flags"));

    dex = valid();
    ClassDef other = cls(dex);
    other.type = "LU;";
    other.superclass = "LT;";
    cls(dex).superclass = "LU;";
    dex.classes.push_back(other);
    EXPECT_TRUE(reports(dex, "inheritance cycle"));
}

TEST(ModelValidate, Fields)
{
    Dex dex = valid();
    FieldDef f;
    f.name = "x";
    f.type = "I";
    cls(dex).fields = {f, f};
    EXPECT_TRUE(reports(dex, "defined twice"));

    dex = valid();
    f.type = "V";
    cls(dex).fields = {f};
    EXPECT_TRUE(reports(dex, "invalid field type"));

    dex = valid();
    f.type = "I";
    f.initial_value = Value{Value::Int{1}};
    cls(dex).fields = {f};
    EXPECT_TRUE(reports(dex, "only a static field"));

    dex = valid();
    f.access = AccessFlags::Static;
    f.initial_value = Value{Value::Long{1}};
    cls(dex).fields = {f};
    EXPECT_TRUE(reports(dex, "does not match the field's type"));

    dex = valid();
    f.initial_value = Value{Value::Int{1}};
    f.access = AccessFlags::Static | AccessFlags::Volatile | AccessFlags::Final;
    cls(dex).fields = {f};
    EXPECT_TRUE(reports(dex, "volatile and final"));

    dex = valid();
    f.access = AccessFlags::Public | AccessFlags::Private;
    cls(dex).fields = {f};
    EXPECT_TRUE(reports(dex, "more than one of public"));
}

TEST(ModelValidate, Methods)
{
    Dex dex = valid();
    cls(dex).methods.push_back(method(dex));
    EXPECT_TRUE(reports(dex, "defined twice"));

    dex = valid();
    method(dex).name = "<foo>";
    EXPECT_TRUE(reports(dex, "only <init> and <clinit>"));

    dex = valid();
    method(dex).code.reset();
    EXPECT_TRUE(reports(dex, "must be abstract or native"));

    dex = valid();
    method(dex).access = AccessFlags::Public | AccessFlags::Abstract;
    EXPECT_TRUE(reports(dex, "cannot have code"));

    dex = valid();
    method(dex).access = AccessFlags::Public | AccessFlags::Constructor;
    EXPECT_TRUE(reports(dex, "can be marked constructor"));

    dex = valid();
    method(dex).name = "<clinit>";
    method(dex).proto = "(I)V";
    EXPECT_TRUE(reports(dex, "()V"));

    dex = valid();
    method(dex).proto = "(JJ)V"; // four argument registers in a frame of four: fine
    EXPECT_TRUE(validate(dex).empty());
    method(dex).proto = "(JJI)V";
    EXPECT_TRUE(reports(dex, "cannot hold the 5 argument words"));

    dex = valid();
    code(dex).items.clear();
    EXPECT_TRUE(reports(dex, "no instructions"));
}

TEST(ModelValidate, InterfacesFollowArtFromVersion37)
{
    Dex dex = valid();
    cls(dex).access = AccessFlags::Public | AccessFlags::Interface | AccessFlags::Abstract;
    MethodDef &m = method(dex);
    // A default method: needs 037.
    m.access = AccessFlags::Public;
    EXPECT_EQ(required_version(dex), 37u);
    auto low = write(dex);
    ASSERT_FALSE(low.has_value());
    EXPECT_EQ(low.error().kind, Error::Kind::VersionTooLow);
    dex.version = 37;
    EXPECT_TRUE(validate(dex).empty());
    // A static interface method is fine in 035 files (ART loads them).
    dex.version = 35;
    m.access = AccessFlags::Public | AccessFlags::Static;
    EXPECT_EQ(required_version(dex), 35u);
    EXPECT_TRUE(validate(dex).empty());
}

TEST(ModelValidate, Code)
{
    Dex dex = valid();
    Label l = code(dex).new_label();
    code(dex).items = {Insn{Goto{l}}};
    EXPECT_TRUE(reports(dex, "is not placed"));

    code(dex).items = {l, l, Insn{Goto{l}}};
    EXPECT_TRUE(reports(dex, "placed twice"));

    code(dex).items = {Insn{Goto{l}}, l};
    EXPECT_TRUE(reports(dex, "not followed by an instruction"));

    code(dex).items = {Insn{Const{4, 0}}, Insn{ReturnVoid{}}};
    EXPECT_TRUE(reports(dex, "outside the 4 registers"));

    code(dex).items = {Insn{ConstWide{3, 0}}, Insn{ReturnVoid{}}};
    EXPECT_TRUE(reports(dex, "(pair) is outside"));

    code(dex).registers = 40;
    code(dex).items = {Insn{InstanceGet{ValueKind::Normal, 20, 1, FieldId{"LT;", "f", "I"}}},
                       Insn{ReturnVoid{}}};
    EXPECT_TRUE(reports(dex, "does not fit the instruction"));

    code(dex).items = {
        Insn{Invoke{Invoke::Kind::Static, {1, 3, 5, 7, 9, 11}, MethodId{"LT;", "m", "()V"}}},
        Insn{ReturnVoid{}}};
    EXPECT_TRUE(reports(dex, "argument registers"));
    code(dex).items = {
        Insn{Invoke{Invoke::Kind::Static, RegList::range(20, 6), MethodId{"LT;", "m", "()V"}}},
        Insn{ReturnVoid{}}};
    EXPECT_FALSE(reports(dex, "argument registers"));

    code(dex).items = {Insn{Switch{0, {{1, l}, {1, l}}}}, l, Insn{ReturnVoid{}}};
    EXPECT_TRUE(reports(dex, "listed twice"));

    code(dex).items = {Insn{FillArrayData{0, 3, {1, 2, 3}}}, Insn{ReturnVoid{}}};
    EXPECT_TRUE(reports(dex, "1, 2, 4 or 8 bytes"));

    code(dex).items = {Insn{BinaryLiteral{LiteralOp::Shl, 0, 1, 300}}, Insn{ReturnVoid{}}};
    EXPECT_TRUE(reports(dex, "8-bit literal"));

    code(dex).items = {Insn{Move{ValueKind::Byte, 0, 1}}, Insn{ReturnVoid{}}};
    EXPECT_TRUE(reports(dex, "only takes the Normal, Wide or Object kind"));

    code(dex).items = {Insn{NewInstance{0, "[I"}}, Insn{ReturnVoid{}}};
    EXPECT_TRUE(reports(dex, "invalid class descriptor"));

    Label start = code(dex).new_label(), end = code(dex).new_label();
    code(dex).items = {end, Insn{ReturnVoid{}}, start};
    code(dex).tries = {TryRange{start, end, {}, l}};
    EXPECT_TRUE(reports(dex, "ends before it starts"));
}

TEST(ModelValidate, ValuesAndAnnotations)
{
    Dex dex = valid();
    AnnotationItem a;
    a.annotation.type = "LA;";
    a.annotation.elements = {{"x", Value{Value::Int{1}}}, {"x", Value{Value::Int{2}}}};
    cls(dex).annotations = {a};
    EXPECT_TRUE(reports(dex, "twice"));

    a.annotation.elements = {
        {"x", Value{MethodHandle{MethodHandleKind::StaticGet, MethodId{"LT;", "m", "()V"}}}}};
    cls(dex).annotations = {a};
    EXPECT_TRUE(reports(dex, "a field method handle names a method"));

    a.annotation.elements = {{"x", Value{Value::String{"\xC3"}}}};
    cls(dex).annotations = {a};
    EXPECT_TRUE(reports(dex, "not valid UTF-8"));

    a.annotation.elements = {};
    cls(dex).annotations = {a, a};
    EXPECT_TRUE(reports(dex, "present twice"));
}

TEST(ModelValidate, NamesWithSpacesAreAcceptedAsArtDoes)
{
    Dex dex = valid();
    method(dex).name = "with space";
    EXPECT_TRUE(validate(dex).empty());
    EXPECT_EQ(required_version(dex), 35u);
}

TEST(ModelWriteErrors, InvalidModelListsEveryProblem)
{
    Dex dex = valid();
    method(dex).name = "<foo>";
    cls(dex).interfaces = {"bad"};
    auto out = write(dex);
    ASSERT_FALSE(out.has_value());
    EXPECT_EQ(out.error().kind, Error::Kind::Invalid);
    EXPECT_GE(out.error().problems.size(), 2u);
}

TEST(ModelWriteErrors, VersionsThatDoNotExist)
{
    Dex dex = valid();
    dex.version = 36;
    EXPECT_TRUE(reports(dex, "unsupported DEX version"));
    dex.version = 41;
    EXPECT_FALSE(write(dex).has_value());
}

TEST(ModelWriteErrors, TooManyTypes)
{
    // 65536 distinct types: one more than a DEX file can hold.
    Dex dex = valid();
    Code &c = code(dex);
    c.items.clear();
    for (int i = 0; i < 65536; ++i)
        c.items.push_back(Insn{ConstClass{0, "LT" + std::to_string(i) + ";"}});
    c.items.push_back(Insn{ReturnVoid{}});
    auto out = write(dex);
    ASSERT_FALSE(out.has_value());
    EXPECT_EQ(out.error().kind, Error::Kind::TooLarge);
    EXPECT_GT(pool_stats(dex).types, 65535u);
}

TEST(ModelRead, RejectsWhatItCannotWriteBack)
{
    auto bytes = write(valid());
    ASSERT_TRUE(bytes.has_value());
    ASSERT_TRUE(read(*bytes).has_value());
    // Find the method's code item and turn its return-void into an unused
    // opcode (0x3e); the reader refuses it instead of guessing.
    auto file = dex::raw::parser::parse_buffer(*bytes);
    ASSERT_TRUE(file.has_value());
    auto data = dex::raw::parser::parse_class_data(*bytes, file->class_defs()[0].class_data_off);
    ASSERT_TRUE(data.has_value());
    uint32_t insns_at = data->direct_methods[0].code_off + 16;
    auto patched = *bytes;
    ASSERT_EQ(patched[insns_at], 0x0e);
    patched[insns_at] = 0x3e;
    auto bad = read(patched);
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().kind, Error::Kind::Malformed);
    EXPECT_NE(bad.error().message.find("LT;->m()V"), std::string::npos) << bad.error().message;
}

TEST(ModelRead, RejectsOtherVersionsAndBadInput)
{
    std::vector<uint8_t> junk = {'n', 'o', 't', ' ', 'd', 'e', 'x', 0};
    auto r = read(junk);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().kind, Error::Kind::Malformed);

    auto bytes = write(valid());
    ASSERT_TRUE(bytes.has_value());
    auto v41 = *bytes;
    v41[4] = '0';
    v41[5] = '4';
    v41[6] = '1';
    r = read(v41);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().kind, Error::Kind::Unsupported);

    auto file = read_file("does/not/exist.dex");
    ASSERT_FALSE(file.has_value());
    EXPECT_EQ(file.error().kind, Error::Kind::Io);
}
