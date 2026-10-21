// rename_types(): every type reference, generic signatures included.

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "../raw/test_utils.hpp"
#include "describe.hpp"
#include "dex_check.hpp"
#include "model/model.hpp"

using namespace dex::model;
using dex::AccessFlags;
using dex::FieldId;
using dex::MethodId;

namespace {

std::optional<std::string> a_to_b(std::string_view type)
{
    if (type.starts_with("La/"))
        return "Lb/" + std::string(type.substr(3));
    return std::nullopt;
}

Value strings(std::vector<std::string> pieces)
{
    Value::Array array;
    for (std::string &p : pieces)
        array.values.push_back(Value{Value::String{std::move(p)}});
    return Value{std::move(array)};
}

AnnotationItem signature(std::vector<std::string> pieces)
{
    AnnotationItem a;
    a.visibility = dex::AnnotationVisibility::System;
    a.annotation.type = "Ldalvik/annotation/Signature;";
    a.annotation.elements = {{"value", strings(std::move(pieces))}};
    return a;
}

std::vector<std::string> pieces_of(const AnnotationItem &a)
{
    std::vector<std::string> out;
    for (const Value &v : std::get<Value::Array>(a.annotation.elements[0].value.data).values)
        out.push_back(std::get<Value::String>(v.data).value);
    return out;
}

} // namespace

TEST(ModelRename, RenamesEveryTypeReference)
{
    Dex dex;
    ClassDef &foo = dex.classes.emplace_back();
    foo.type = "La/Foo;";
    foo.superclass = "Ljava/lang/Object;";
    foo.interfaces = {"La/I;"};
    foo.annotations = {signature({"Ljava/lang/Object;", "La/I<", "La/Foo;", ">;"})};
    FieldDef &f = foo.fields.emplace_back();
    f.name = "arr";
    f.type = "[[La/Foo;";
    f.access = AccessFlags::Static;
    f.initial_value = Value{Value::Type{"La/Bar;"}};
    MethodDef &m = foo.methods.emplace_back();
    m.name = "m";
    m.proto = "(La/I;[La/Foo;)La/Bar;";
    m.access = AccessFlags::Static;
    m.annotations = {signature({"(", "Ljava/util/List<", "La/Foo;", ">;)", "La/Bar;"})};
    Code &code = m.code.emplace();
    code.registers = 4;
    // One shared string used both as a type and as a string constant: only
    // the type is renamed.
    Shared<std::string> text = std::string("La/Foo;");
    Label start = code.new_label(), end = code.new_label(), handler = code.new_label();
    code.items = {
        start,
        Insn{ConstString{0, text}},
        Insn{ConstClass{0, text}},
        Insn{CheckCast{0, "[La/Foo;"}},
        Insn{InstanceGet{ValueKind::Object, 1, 0, FieldId{"La/Bar;", "f", "La/Foo;"}}},
        Insn{Invoke{
            Invoke::Kind::Static, {0, 1}, MethodId{"La/Foo;", "m", "(La/I;[La/Foo;)La/Bar;"}}},
        LocalStart{0, LocalVariable{"list", "Ljava/util/List;", "Ljava/util/List<La/Foo;>;"}},
        end,
        Insn{ReturnVoid{}},
        handler,
        Insn{MoveException{0}},
        Insn{ReturnVoid{}},
    };
    code.tries = {TryRange{start, end, {Catch{"La/Ex;", handler}}, std::nullopt}};

    rename_types(dex, a_to_b);

    const ClassDef &c = dex.classes[0];
    EXPECT_EQ(c.type, "Lb/Foo;");
    EXPECT_EQ(c.interfaces, (std::vector<std::string>{"Lb/I;"}));
    EXPECT_EQ(pieces_of(c.annotations[0]),
              (std::vector<std::string>{"Ljava/lang/Object;", "Lb/I<", "Lb/Foo;", ">;"}));
    EXPECT_EQ(c.fields[0].type, "[[Lb/Foo;");
    EXPECT_EQ(c.fields[0].initial_value, Value{Value::Type{"Lb/Bar;"}});
    const MethodDef &rm = c.methods[0];
    EXPECT_EQ(rm.proto, "(Lb/I;[Lb/Foo;)Lb/Bar;");
    EXPECT_EQ(pieces_of(rm.annotations[0]),
              (std::vector<std::string>{"(", "Ljava/util/List<", "Lb/Foo;", ">;)", "Lb/Bar;"}));
    const auto &items = rm.code->items;
    auto insn = [&](std::size_t i) -> const Insn & { return std::get<Insn>(items[i]); };
    EXPECT_EQ(std::get<ConstString>(insn(1)).value.get(), "La/Foo;"); // a string: kept
    EXPECT_EQ(std::get<ConstClass>(insn(2)).type.get(), "Lb/Foo;");
    EXPECT_EQ(std::get<CheckCast>(insn(3)).type.get(), "[Lb/Foo;");
    EXPECT_EQ(std::get<InstanceGet>(insn(4)).field.get(), (FieldId{"Lb/Bar;", "f", "Lb/Foo;"}));
    EXPECT_EQ(std::get<Invoke>(insn(5)).method.get(),
              (MethodId{"Lb/Foo;", "m", "(Lb/I;[Lb/Foo;)Lb/Bar;"}));
    EXPECT_EQ(std::get<LocalStart>(items[6]).local->signature, "Ljava/util/List<Lb/Foo;>;");
    EXPECT_EQ(rm.code->tries[0].catches[0].type, "Lb/Ex;");
}

TEST(ModelRename, GenericSignaturesKeepInnerSuffixesAndTypeVariables)
{
    Dex dex;
    ClassDef &c = dex.classes.emplace_back();
    c.type = "La/Outer;";
    c.superclass = "Ljava/lang/Object;";
    // A type parameter named L must not be taken for a class.
    c.annotations = {
        signature({"<L:", "La/Foo;", ">", "La/Outer<TL;>.Inner<TL;>;", "Ljava/lang/Object;"})};
    rename_types(dex, a_to_b);
    EXPECT_EQ(pieces_of(dex.classes[0].annotations[0]),
              (std::vector<std::string>{"<L:", "Lb/Foo;", ">", "Lb/Outer<TL;>.Inner<TL;>;",
                                        "Ljava/lang/Object;"}));

    // A class name split across pieces: the signature becomes one piece.
    dex.classes[0].annotations = {signature({"Ljava/util/List<La/F", "oo;>;"})};
    rename_types(dex, a_to_b);
    EXPECT_EQ(pieces_of(dex.classes[0].annotations[0]),
              (std::vector<std::string>{"Ljava/util/List<Lb/Foo;>;", ""}));

    // Not a signature: left alone.
    dex.classes[0].annotations = {signature({"La/Foo"})};
    rename_types(dex, a_to_b);
    EXPECT_EQ(pieces_of(dex.classes[0].annotations[0]), (std::vector<std::string>{"La/Foo"}));
}

TEST(ModelRename, MovesAPackageInARealApp)
{
    auto input = load_buf("tests/data/androguard/Annotation_classes.dex");
    auto dex = read(input);
    ASSERT_TRUE(dex.has_value());
    rename_types(*dex, [](std::string_view type) -> std::optional<std::string> {
        if (type.starts_with("Landroid/support/"))
            return "Lshaded/support/" + std::string(type.substr(17));
        return std::nullopt;
    });
    auto out = write(*dex);
    ASSERT_TRUE(out.has_value()) << out.error().message;
    for (const std::string &p : check_dex(*out))
        ADD_FAILURE() << p;

    std::string path = temp_file("renamed", *out);
    auto ctx = dex::AnalysisContext::from_dex(path);
    std::filesystem::remove(path);
    ASSERT_TRUE(ctx.has_value());
    std::size_t moved = 0;
    for (const std::string &line : describe(*ctx)) {
        if (line.find(" loads \"") != std::string::npos)
            continue; // string constants are not renamed
        EXPECT_EQ(line.find("android/support/"), std::string::npos) << line;
        moved += line.find("Lshaded/support/") != std::string::npos ? 1 : 0;
    }
    EXPECT_GT(moved, 1000u);
}
