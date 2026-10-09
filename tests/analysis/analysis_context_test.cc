#include <gtest/gtest.h>

#include <optional>

#include "dex.hpp"

using namespace dex;

class FromAnyDexTest : public ::testing::TestWithParam<std::string>
{
};

TEST_P(FromAnyDexTest, FromDexSucceeds)
{
    auto ctx = AnalysisContext::from_dex(GetParam());
    ASSERT_TRUE(ctx.has_value()) << ctx.error().message;
}

TEST_P(FromAnyDexTest, FromDexFilesSucceeds)
{
    auto ctx = AnalysisContext::from_dex_files({GetParam()});
    ASSERT_TRUE(ctx.has_value()) << ctx.error().message;
}

INSTANTIATE_TEST_SUITE_P(AllDexFiles, FromAnyDexTest,
                         ::testing::Values("tests/data/classes.dex", "tests/data/multistring.dex",
                                           "tests/data/shapes.dex", "tests/data/trycatch.dex",
                                           "tests/data/polymorphic.dex"),
                         [](const testing::TestParamInfo<FromAnyDexTest::ParamType> &info) {
                             std::string stem = info.param;
                             auto slash = stem.rfind('/');
                             if (slash != std::string::npos)
                                 stem = stem.substr(slash + 1);
                             auto dot = stem.rfind('.');
                             if (dot != std::string::npos)
                                 stem = stem.substr(0, dot);
                             return stem;
                         });

TEST(AnalysisContextTest, FromDexReturnsFileNotFoundForMissingFile)
{
    auto ctx = AnalysisContext::from_dex("tests/data/nonexistent.dex");
    ASSERT_FALSE(ctx.has_value());
    EXPECT_EQ(ctx.error().code, AnalysisError::Code::FileNotFound);
}

TEST(AnalysisContextTest, ClassesYieldsCorrectCount)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());

    auto classes = ctx->classes();
    EXPECT_EQ(classes.size(), 1u);
}

TEST(AnalysisContextTest, FindClassHitsForPresentClass)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());

    auto cls = ctx->find_class("LTestDex;");
    ASSERT_TRUE(cls.has_value());
    EXPECT_TRUE(cls->is_valid());
}

TEST(AnalysisContextTest, FindClassMissesForAbsentClass)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());

    auto cls = ctx->find_class("LNonExistent;");
    EXPECT_FALSE(cls.has_value());
}

TEST(ClassTest, NameReturnsDescriptor)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());

    auto cls = ctx->find_class("LTestDex;");
    ASSERT_TRUE(cls.has_value());
    EXPECT_EQ(cls->name(), "LTestDex;");
}

TEST(ClassTest, PackageIsEmptyForDefaultPackageClass)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());

    auto cls = ctx->find_class("LTestDex;");
    ASSERT_TRUE(cls.has_value());
    // "LTestDex;" has no '/' so package should be empty
    EXPECT_TRUE(cls->package().empty());
}

TEST(ClassTest, AccessFlagsReturnsPublic)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());

    auto cls = ctx->find_class("LTestDex;");
    ASSERT_TRUE(cls.has_value());
    EXPECT_TRUE(cls->is_public());
    EXPECT_FALSE(cls->is_abstract());
    EXPECT_FALSE(cls->is_interface());
}

TEST(ClassTest, SuperclassIsObject)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());

    auto cls = ctx->find_class("LTestDex;");
    ASSERT_TRUE(cls.has_value());

    auto super = cls->superclass();
    ASSERT_TRUE(super.has_value());
    EXPECT_EQ(super->descriptor(), "Ljava/lang/Object;");
}

TEST(ClassTest, InterfacesIsEmpty)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());

    auto cls = ctx->find_class("LTestDex;");
    ASSERT_TRUE(cls.has_value());
    EXPECT_TRUE(cls->interfaces().empty());
}

TEST(ClassTest, SourceFilePresent)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());

    auto cls = ctx->find_class("LTestDex;");
    ASSERT_TRUE(cls.has_value());

    auto src = cls->source_file();
    ASSERT_TRUE(src.has_value());
    EXPECT_EQ(*src, "TestDex.java");
}

TEST(ClassTest, MethodCountIsThree)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());

    auto cls = ctx->find_class("LTestDex;");
    ASSERT_TRUE(cls.has_value());

    auto methods = cls->methods();
    EXPECT_EQ(methods.size(), 3u);
}

TEST(ClassTest, FieldCountIsZero)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());

    auto cls = ctx->find_class("LTestDex;");
    ASSERT_TRUE(cls.has_value());

    auto fields = cls->fields();
    EXPECT_EQ(fields.size(), 0u);
}

TEST(MethodTest, NameAndConstructorFlag)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());

    auto cls = ctx->find_class("LTestDex;");
    ASSERT_TRUE(cls.has_value());

    auto methods = cls->methods();
    ASSERT_EQ(methods.size(), 3u);

    EXPECT_EQ(methods[0].name(), "<init>");
    EXPECT_TRUE(methods[0].is_constructor());

    EXPECT_EQ(methods[1].name(), "helloDex");
    EXPECT_FALSE(methods[1].is_constructor());

    EXPECT_EQ(methods[2].name(), "main");
    EXPECT_FALSE(methods[2].is_constructor());
}

TEST(MethodTest, ReturnTypeVoid)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());

    auto cls = ctx->find_class("LTestDex;");
    ASSERT_TRUE(cls.has_value());

    auto methods = cls->methods();
    ASSERT_EQ(methods.size(), 3u);

    // <init> returns void
    auto rt = methods[0].return_type();
    EXPECT_EQ(rt.descriptor(), "V");
    EXPECT_TRUE(rt.is_primitive());
    EXPECT_FALSE(rt.is_class());
    EXPECT_FALSE(rt.is_array());
}

TEST(MethodTest, ReturnTypeString)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());

    auto cls = ctx->find_class("LTestDex;");
    ASSERT_TRUE(cls.has_value());

    auto methods = cls->methods();
    ASSERT_EQ(methods.size(), 3u);

    // helloDex() returns String
    auto rt = methods[1].return_type();
    EXPECT_EQ(rt.descriptor(), "Ljava/lang/String;");
    EXPECT_TRUE(rt.is_class());
    EXPECT_FALSE(rt.is_primitive());
}

TEST(MethodTest, MainParametersContainStringArray)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());

    auto cls = ctx->find_class("LTestDex;");
    ASSERT_TRUE(cls.has_value());

    auto methods = cls->methods();
    ASSERT_EQ(methods.size(), 3u);

    // main(String[]) has one parameter
    auto params = methods[2].parameters();
    ASSERT_EQ(params.size(), 1u);
    EXPECT_EQ(params[0].descriptor(), "[Ljava/lang/String;");
    EXPECT_TRUE(params[0].is_array());

    auto elem = params[0].element_type();
    ASSERT_TRUE(elem.has_value());
    EXPECT_EQ(elem->descriptor(), "Ljava/lang/String;");
    EXPECT_TRUE(elem->is_class());
}

TEST(MethodTest, InitHasNoParameters)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());

    auto cls = ctx->find_class("LTestDex;");
    ASSERT_TRUE(cls.has_value());

    auto methods = cls->methods();
    ASSERT_EQ(methods.size(), 3u);

    EXPECT_TRUE(methods[0].parameters().empty());
}

TEST(MethodTest, DeclaringClassDescriptor)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());

    auto cls = ctx->find_class("LTestDex;");
    ASSERT_TRUE(cls.has_value());

    auto methods = cls->methods();
    ASSERT_FALSE(methods.empty());

    EXPECT_EQ(methods[0].declaring_class().descriptor(), "LTestDex;");
}

TEST(TypeDescriptorTest, Classification)
{
    TypeDescriptor cls("Ljava/lang/Object;");
    EXPECT_TRUE(cls.is_class());
    EXPECT_FALSE(cls.is_array());
    EXPECT_FALSE(cls.is_primitive());

    TypeDescriptor arr("[Ljava/lang/String;");
    EXPECT_FALSE(arr.is_class());
    EXPECT_TRUE(arr.is_array());
    EXPECT_FALSE(arr.is_primitive());

    TypeDescriptor prim_v("V");
    EXPECT_FALSE(prim_v.is_class());
    EXPECT_FALSE(prim_v.is_array());
    EXPECT_TRUE(prim_v.is_primitive());

    TypeDescriptor prim_i("I");
    EXPECT_TRUE(prim_i.is_primitive());

    TypeDescriptor prim_z("Z");
    EXPECT_TRUE(prim_z.is_primitive());
}

TEST(TypeDescriptorTest, ElementTypeForArray)
{
    TypeDescriptor arr("[I");
    auto elem = arr.element_type();
    ASSERT_TRUE(elem.has_value());
    EXPECT_EQ(elem->descriptor(), "I");
    EXPECT_TRUE(elem->is_primitive());

    TypeDescriptor nested("[[Ljava/lang/String;");
    auto elem2 = nested.element_type();
    ASSERT_TRUE(elem2.has_value());
    EXPECT_EQ(elem2->descriptor(), "[Ljava/lang/String;");
    EXPECT_TRUE(elem2->is_array());
}

TEST(TypeDescriptorTest, ElementTypeNulloptForNonArray)
{
    TypeDescriptor cls("Ljava/lang/Object;");
    EXPECT_FALSE(cls.element_type().has_value());

    TypeDescriptor prim("I");
    EXPECT_FALSE(prim.element_type().has_value());
}

TEST(TypeDescriptorTest, AsClassRefReturnsRefForClass)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());

    TypeDescriptor td("LTestDex;");
    auto ref = td.as_class_ref(&*ctx);
    ASSERT_TRUE(ref.has_value());
    EXPECT_EQ(ref->descriptor(), "LTestDex;");
}

TEST(TypeDescriptorTest, AsClassRefNulloptForPrimitive)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());

    TypeDescriptor prim("V");
    EXPECT_FALSE(prim.as_class_ref(&*ctx).has_value());
}

TEST(ClassRefTest, ResolveHitsForKnownClass)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());

    ClassRef ref("LTestDex;", &*ctx);
    EXPECT_TRUE(ref.is_resolved());

    auto cls = ref.resolve();
    ASSERT_TRUE(cls.is_valid());
    EXPECT_EQ(cls.name(), "LTestDex;");
}

TEST(ClassRefTest, ResolveMissesForUnknownClass)
{
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());

    ClassRef ref("LUnknown;", &*ctx);
    EXPECT_FALSE(ref.is_resolved());
    EXPECT_FALSE(ref.resolve().is_valid());
}

// ---------------------------------------------------------------------------
// shapes.dex — interface hierarchy, abstract class, multi-field classes
//
// class_defs order (baksmali ground truth):
//   [0] LShape;         interface|abstract
//   [1] LAbstractShape; abstract, implements LShape;
//   [2] LCircle;        extends LAbstractShape;
//   [3] LRectangle;     extends LAbstractShape;
// ---------------------------------------------------------------------------

class ShapesDexTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        auto result = AnalysisContext::from_dex("tests/data/shapes.dex");
        ASSERT_TRUE(result.has_value()) << result.error().message;
        ctx_.emplace(std::move(*result));
    }
    std::optional<AnalysisContext> ctx_;
    const AnalysisContext &ctx() const { return *ctx_; }
};

TEST_F(ShapesDexTest, FourClasses) { EXPECT_EQ(ctx().classes().size(), 4u); }

TEST_F(ShapesDexTest, ShapeIsInterfaceAndAbstract)
{
    auto cls = ctx().find_class("LShape;");
    ASSERT_TRUE(cls.has_value());
    EXPECT_TRUE(cls->is_interface());
    EXPECT_TRUE(cls->is_abstract());
}

TEST_F(ShapesDexTest, AbstractShapeIsAbstractNotInterface)
{
    auto cls = ctx().find_class("LAbstractShape;");
    ASSERT_TRUE(cls.has_value());
    EXPECT_TRUE(cls->is_abstract());
    EXPECT_FALSE(cls->is_interface());
}

TEST_F(ShapesDexTest, CircleSuperclassIsAbstractShape)
{
    auto cls = ctx().find_class("LCircle;");
    ASSERT_TRUE(cls.has_value());
    auto super = cls->superclass();
    ASSERT_TRUE(super.has_value());
    EXPECT_EQ(super->descriptor(), "LAbstractShape;");
}

TEST_F(ShapesDexTest, AbstractShapeImplementsShape)
{
    auto cls = ctx().find_class("LAbstractShape;");
    ASSERT_TRUE(cls.has_value());
    auto ifaces = cls->interfaces();
    ASSERT_EQ(ifaces.size(), 1u);
    EXPECT_EQ(ifaces[0].descriptor(), "LShape;");
}

TEST_F(ShapesDexTest, AbstractShapeColorField)
{
    auto cls = ctx().find_class("LAbstractShape;");
    ASSERT_TRUE(cls.has_value());
    auto fields = cls->fields();
    ASSERT_EQ(fields.size(), 1u);
    EXPECT_EQ(fields[0].name(), "color");
    EXPECT_EQ(fields[0].type().descriptor(), "Ljava/lang/String;");
}

TEST_F(ShapesDexTest, AbstractShapeColorFieldIsProtected)
{
    auto cls = ctx().find_class("LAbstractShape;");
    ASSERT_TRUE(cls.has_value());
    auto fields = cls->fields();
    ASSERT_EQ(fields.size(), 1u);
    EXPECT_TRUE(has_flag(fields[0].access_flags(), AccessFlags::Protected));
    EXPECT_FALSE(has_flag(fields[0].access_flags(), AccessFlags::Private));
}

TEST_F(ShapesDexTest, CircleRadiusField)
{
    auto cls = ctx().find_class("LCircle;");
    ASSERT_TRUE(cls.has_value());
    auto fields = cls->fields();
    ASSERT_EQ(fields.size(), 1u);
    EXPECT_EQ(fields[0].name(), "radius");
    EXPECT_EQ(fields[0].type().descriptor(), "D");
    EXPECT_TRUE(has_flag(fields[0].access_flags(), AccessFlags::Private));
}

TEST_F(ShapesDexTest, RectangleTwoFields)
{
    auto cls = ctx().find_class("LRectangle;");
    ASSERT_TRUE(cls.has_value());
    auto fields = cls->fields();
    ASSERT_EQ(fields.size(), 2u);
    // field_ids order: height (field_id[2]) then width (field_id[3])
    EXPECT_EQ(fields[0].name(), "height");
    EXPECT_EQ(fields[1].name(), "width");
    EXPECT_EQ(fields[0].type().descriptor(), "D");
    EXPECT_EQ(fields[1].type().descriptor(), "D");
}

TEST_F(ShapesDexTest, CircleConstructorTwoParameters)
{
    auto cls = ctx().find_class("LCircle;");
    ASSERT_TRUE(cls.has_value());
    auto methods = cls->methods();
    ASSERT_FALSE(methods.empty());
    // direct methods come first; <init> is the only direct method
    EXPECT_EQ(methods[0].name(), "<init>");
    auto params = methods[0].parameters();
    ASSERT_EQ(params.size(), 2u);
    EXPECT_EQ(params[0].descriptor(), "Ljava/lang/String;");
    EXPECT_EQ(params[1].descriptor(), "D");
}

TEST_F(ShapesDexTest, RectangleConstructorThreeParameters)
{
    auto cls = ctx().find_class("LRectangle;");
    ASSERT_TRUE(cls.has_value());
    auto methods = cls->methods();
    ASSERT_FALSE(methods.empty());
    EXPECT_EQ(methods[0].name(), "<init>");
    auto params = methods[0].parameters();
    ASSERT_EQ(params.size(), 3u);
    EXPECT_EQ(params[0].descriptor(), "Ljava/lang/String;");
    EXPECT_EQ(params[1].descriptor(), "D");
    EXPECT_EQ(params[2].descriptor(), "D");
}

// ---------------------------------------------------------------------------
// trycatch.dex — try/catch, try/catch/finally
//
// method order in class_data (direct only):
//   [0] <init>()V
//   [1] divide(II)I
//   [2] safeParse(String)String
// ---------------------------------------------------------------------------

class TryCatchDexTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        auto result = AnalysisContext::from_dex("tests/data/trycatch.dex");
        ASSERT_TRUE(result.has_value()) << result.error().message;
        ctx_.emplace(std::move(*result));
    }
    std::optional<AnalysisContext> ctx_;
    const AnalysisContext &ctx() const { return *ctx_; }
};

TEST_F(TryCatchDexTest, OneClass) { EXPECT_EQ(ctx().classes().size(), 1u); }

TEST_F(TryCatchDexTest, ThreeMethods)
{
    auto cls = ctx().find_class("LTryCatch;");
    ASSERT_TRUE(cls.has_value());
    EXPECT_EQ(cls->methods().size(), 3u);
}

TEST_F(TryCatchDexTest, DivideReturnTypeAndParameters)
{
    auto cls = ctx().find_class("LTryCatch;");
    ASSERT_TRUE(cls.has_value());
    auto methods = cls->methods();
    ASSERT_EQ(methods.size(), 3u);

    // methods()[1] = divide(II)I
    EXPECT_EQ(methods[1].name(), "divide");
    EXPECT_EQ(methods[1].return_type().descriptor(), "I");
    auto params = methods[1].parameters();
    ASSERT_EQ(params.size(), 2u);
    EXPECT_EQ(params[0].descriptor(), "I");
    EXPECT_EQ(params[1].descriptor(), "I");
}

TEST_F(TryCatchDexTest, SafeParseReturnTypeAndParameter)
{
    auto cls = ctx().find_class("LTryCatch;");
    ASSERT_TRUE(cls.has_value());
    auto methods = cls->methods();
    ASSERT_EQ(methods.size(), 3u);

    // methods()[2] = safeParse(String)String
    EXPECT_EQ(methods[2].name(), "safeParse");
    EXPECT_EQ(methods[2].return_type().descriptor(), "Ljava/lang/String;");
    auto params = methods[2].parameters();
    ASSERT_EQ(params.size(), 1u);
    EXPECT_EQ(params[0].descriptor(), "Ljava/lang/String;");
}

// ===== Lifetime tests for handle/AnalysisContext shared ownership. =====

TEST(LifetimeTest, HandleSurvivesContextCopyDrop)
{
    auto exp = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(exp.has_value()) << exp.error().message;

    std::optional<AnalysisContext> ctx_a;
    ctx_a.emplace(std::move(*exp));

    AnalysisContext ctx_b = *ctx_a;
    auto cls = ctx_b.find_class("LTestDex;");
    ASSERT_TRUE(cls.has_value());

    ctx_a.reset();

    EXPECT_EQ(cls->name(), "LTestDex;");
    EXPECT_FALSE(cls->methods().empty());
}

TEST(LifetimeTest, HandleSurvivesAllContextsDropped)
{
    std::optional<Class> cls;
    {
        auto exp = AnalysisContext::from_dex("tests/data/classes.dex");
        ASSERT_TRUE(exp.has_value()) << exp.error().message;
        cls = exp->find_class("LTestDex;");
    }
    ASSERT_TRUE(cls.has_value());
    EXPECT_EQ(cls->name(), "LTestDex;");
    EXPECT_FALSE(cls->methods().empty());
}
