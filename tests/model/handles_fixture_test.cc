// The source of tests/data/handles.dex.
//
// javac and d8 cannot make the instructions this fixture is for:
// const-method-handle, const-method-type and invoke-custom with a bootstrap
// method of our own.  So the model builds the file, from handles_model()
// below.  The test checks that the committed file still holds exactly that
// model.  After changing handles_model(), write the file again from the
// project root with the disabled test HandlesFixture.DISABLED_WriteTheFile:
//
//     ./build/unit_tests --gtest_also_run_disabled_tests --gtest_filter='*WriteTheFile'

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "model/model.hpp"

using namespace dex::model;
using dex::AccessFlags;
using dex::FieldId;
using dex::MethodHandleKind;
using dex::MethodId;

namespace {

constexpr const char *kPath = "tests/data/handles.dex";

constexpr const char *kBootstrapProto = "(Ljava/lang/invoke/MethodHandles$Lookup;Ljava/lang/String;"
                                        "Ljava/lang/invoke/MethodType;)Ljava/lang/invoke/CallSite;";

MethodDef static_method(std::string name, std::string proto, Reg registers, std::vector<Item> items)
{
    MethodDef m;
    m.name = std::move(name);
    m.proto = std::move(proto);
    m.access = AccessFlags::Public | AccessFlags::Static;
    Code &code = m.code.emplace();
    code.registers = registers;
    code.items = std::move(items);
    return m;
}

/** One class, LHandles;, written as DEX 039 (const-method-handle needs it):
 *
 *      @HandleMarker(getter = <static-get Handles.counter>,
 *                    invoker = <invoke-static Handles.target(I)I>,
 *                    type = <method type (I)I>)
 *      public class Handles {
 *          public static int counter;
 *          public static int target(int x);           // return x
 *          public static CallSite bootstrap(Lookup, String, MethodType);  // return null
 *          // invoke-polymorphic, array-length, const-method-handle,
 *          // const-method-type, invoke-custom:
 *          public static Object run(MethodHandle mh, Object[] args);
 *          // invoke-polymorphic/range (six argument registers):
 *          public static void spread(MethodHandle mh, int a, int b, int c, int d, int e);
 *      } */
Dex handles_model()
{
    const MethodId target{"LHandles;", "target", "(I)I"};
    const MethodHandle target_handle{MethodHandleKind::InvokeStatic, target};

    Dex dex;
    dex.version = 39;
    ClassDef &c = dex.classes.emplace_back();
    c.type = "LHandles;";
    c.access = AccessFlags::Public;
    c.superclass = "Ljava/lang/Object;";

    AnnotationItem marker;
    marker.visibility = dex::AnnotationVisibility::Runtime;
    marker.annotation.type = "LHandleMarker;";
    marker.annotation.elements = {
        {"getter",
         Value{MethodHandle{MethodHandleKind::StaticGet, FieldId{"LHandles;", "counter", "I"}}}},
        {"invoker", Value{target_handle}},
        {"type", Value{Value::MethodType{"(I)I"}}},
    };
    c.annotations.push_back(marker);

    FieldDef &counter = c.fields.emplace_back();
    counter.name = "counter";
    counter.type = "I";
    counter.access = AccessFlags::Public | AccessFlags::Static;

    c.methods.push_back(static_method("target", "(I)I", 1, {Insn{Return{ValueKind::Normal, 0}}}));
    c.methods.push_back(static_method("bootstrap", kBootstrapProto, 4,
                                      {Insn{Const{0, 0}}, Insn{Return{ValueKind::Object, 0}}}));

    CallSite site;
    site.bootstrap = MethodHandle{MethodHandleKind::InvokeStatic,
                                  MethodId{"LHandles;", "bootstrap", kBootstrapProto}};
    site.method_name = "apply";
    site.method_type = "(I)V";

    // v0..v3 are locals; the arguments are v4 (mh) and v5 (args).
    c.methods.push_back(static_method(
        "run", "(Ljava/lang/invoke/MethodHandle;[Ljava/lang/Object;)Ljava/lang/Object;", 6,
        {
            Insn{InvokePolymorphic{RegList{4, 5},
                                   MethodId{"Ljava/lang/invoke/MethodHandle;", "invokeExact",
                                            "([Ljava/lang/Object;)Ljava/lang/Object;"},
                                   std::string("([Ljava/lang/Object;)Ljava/lang/Object;")}},
            Insn{MoveResult{ValueKind::Object, 0}},
            Insn{ArrayLength{1, 5}},
            Insn{ConstMethodHandle{2, target_handle}},
            Insn{ConstMethodType{3, std::string("(I)I")}},
            Insn{InvokeCustom{RegList{1}, site}},
            Insn{Return{ValueKind::Object, 0}},
        }));

    // Six argument registers do not fit the 45cc format: write() picks 4rcc.
    c.methods.push_back(static_method(
        "spread", "(Ljava/lang/invoke/MethodHandle;IIIII)V", 6,
        {
            Insn{InvokePolymorphic{RegList{0, 1, 2, 3, 4, 5},
                                   MethodId{"Ljava/lang/invoke/MethodHandle;", "invoke",
                                            "([Ljava/lang/Object;)Ljava/lang/Object;"},
                                   std::string("(IIIII)V")}},
            Insn{ReturnVoid{}},
        }));
    return dex;
}

std::vector<uint8_t> write_ok(const Dex &dex)
{
    auto out = write(dex);
    if (!out) {
        ADD_FAILURE() << out.error().message;
        for (const dex::Problem &p : out.error().problems)
            ADD_FAILURE() << p.where << ": " << p.message;
        return {};
    }
    return std::move(*out);
}

} // namespace

TEST(HandlesFixture, DISABLED_WriteTheFile)
{
    auto written = write_file(handles_model(), kPath);
    ASSERT_TRUE(written.has_value()) << written.error().message;
}

TEST(HandlesFixture, FileHoldsTheModel)
{
    const Dex model = handles_model();
    auto file = read_file(kPath);
    ASSERT_TRUE(file.has_value()) << file.error().message;
    // Compare what the two write, not the models: reading numbers labels and
    // call-site keys its own way.  The writer's output is canonical, so equal
    // bytes mean equal content.
    EXPECT_EQ(write_ok(*file), write_ok(model))
        << kPath << " is out of date; see the comment at the top of this file";
}
