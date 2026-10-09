#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "dex.hpp"

using namespace dex;

namespace {

bool contains(const std::vector<std::string_view> &v, std::string_view s)
{
    return std::find(v.begin(), v.end(), s) != v.end();
}

} // namespace

// shapes.dex ground truth:
//   LShape;         interface, abstract
//   LAbstractShape; abstract, implements LShape;
//   LCircle;        extends LAbstractShape;
//   LRectangle;     extends LAbstractShape;

class ShapesHierarchy : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        auto result = AnalysisContext::from_dex("tests/data/shapes.dex");
        ASSERT_TRUE(result.has_value()) << result.error().message;
        ctx_.emplace(std::move(*result));
    }

    AnalysisContext &ctx() { return *ctx_; }
    const ClassHierarchy &h() { return ctx_->class_hierarchy(); }

    std::optional<AnalysisContext> ctx_;
};

TEST_F(ShapesHierarchy, DirectSupertypesCircle)
{
    auto sups = h().supertypes("LCircle;");
    ASSERT_EQ(sups.size(), 1u);
    EXPECT_EQ(sups[0], "LAbstractShape;");
}

TEST_F(ShapesHierarchy, DirectSupertypesAbstractShape)
{
    // superclass + 1 interface
    auto sups = h().supertypes("LAbstractShape;");
    ASSERT_GE(sups.size(), 2u);
    EXPECT_EQ(sups[0], "Ljava/lang/Object;") << "Order: superclass first";
    EXPECT_TRUE(contains(sups, "LShape;"));
}

TEST_F(ShapesHierarchy, AllSupertypesCircleReachesObjectAndShape)
{
    auto all = h().all_supertypes("LCircle;");
    EXPECT_TRUE(contains(all, "LAbstractShape;"));
    EXPECT_TRUE(contains(all, "LShape;")) << "Inherited interface must appear";
    EXPECT_TRUE(contains(all, "Ljava/lang/Object;")) << "Object must be reachable";
}

TEST_F(ShapesHierarchy, ObjectIsExternalNotLoaded)
{
    // shapes.dex doesn't define Object; it's a descriptor-only node.
    EXPECT_FALSE(h().is_loaded("Ljava/lang/Object;"));
    EXPECT_FALSE(h().is_interface("Ljava/lang/Object;"));
    // But it IS in the graph: queries against it succeed (Circle reaches it).
    EXPECT_TRUE(h().is_subtype_of("LCircle;", "Ljava/lang/Object;"));
}

TEST_F(ShapesHierarchy, IsLoadedTrueForFixtureClasses)
{
    EXPECT_TRUE(h().is_loaded("LShape;"));
    EXPECT_TRUE(h().is_loaded("LAbstractShape;"));
    EXPECT_TRUE(h().is_loaded("LCircle;"));
    EXPECT_TRUE(h().is_loaded("LRectangle;"));
}

TEST_F(ShapesHierarchy, IsInterfaceFlag)
{
    EXPECT_TRUE(h().is_interface("LShape;"));
    EXPECT_FALSE(h().is_interface("LAbstractShape;"));
    EXPECT_FALSE(h().is_interface("LCircle;"));
}

TEST_F(ShapesHierarchy, SubclassesOfAbstractShape)
{
    auto subs = h().subclasses("LAbstractShape;");
    EXPECT_EQ(subs.size(), 2u);
    EXPECT_TRUE(contains(subs, "LCircle;"));
    EXPECT_TRUE(contains(subs, "LRectangle;"));
}

TEST_F(ShapesHierarchy, ImplementersOfShape)
{
    auto impls = h().implementers("LShape;");
    ASSERT_EQ(impls.size(), 1u);
    EXPECT_EQ(impls[0], "LAbstractShape;") << "Only AbstractShape directly implements Shape";
}

TEST_F(ShapesHierarchy, AllDescendantsOfShapeIncludesConcreteClasses)
{
    auto desc = h().all_descendants("LShape;");
    // AbstractShape is a direct implementer; Circle and Rectangle inherit
    // through AbstractShape.
    EXPECT_TRUE(contains(desc, "LAbstractShape;"));
    EXPECT_TRUE(contains(desc, "LCircle;"));
    EXPECT_TRUE(contains(desc, "LRectangle;"));
}

TEST_F(ShapesHierarchy, IsSubtypeReflexiveForLoadedClass)
{
    EXPECT_TRUE(h().is_subtype_of("LCircle;", "LCircle;"));
}

TEST_F(ShapesHierarchy, IsSubtypeReturnsFalseForUnknownClass)
{
    EXPECT_FALSE(h().is_subtype_of("LNeverHeardOf;", "Ljava/lang/Object;"));
}

TEST_F(ShapesHierarchy, IsSubtypeAcrossInterface)
{
    EXPECT_TRUE(h().is_subtype_of("LCircle;", "LShape;"))
        << "Circle inherits Shape through AbstractShape";
}

TEST_F(ShapesHierarchy, IsSubtypeStrictHierarchy)
{
    EXPECT_TRUE(h().is_subtype_of("LCircle;", "LAbstractShape;"));
    EXPECT_FALSE(h().is_subtype_of("LAbstractShape;", "LCircle;"))
        << "Subtype relation is anti-symmetric (not reflexive on cousins)";
    EXPECT_FALSE(h().is_subtype_of("LCircle;", "LRectangle;"));
}

TEST_F(ShapesHierarchy, ReturnsSameReferenceAcrossCalls)
{
    const auto &h1 = ctx().class_hierarchy();
    const auto &h2 = ctx().class_hierarchy();
    EXPECT_EQ(&h1, &h2) << "class_hierarchy() must return cached reference";
}

TEST_F(ShapesHierarchy, EmptyResultsForUnknownDescriptor)
{
    EXPECT_TRUE(h().supertypes("LNeverHeardOf;").empty());
    EXPECT_TRUE(h().subclasses("LNeverHeardOf;").empty());
    EXPECT_TRUE(h().all_supertypes("LNeverHeardOf;").empty());
    EXPECT_TRUE(h().all_descendants("LNeverHeardOf;").empty());
    EXPECT_FALSE(h().is_loaded("LNeverHeardOf;"));
}

// ===== Multi-DEX =====

TEST(ClassHierarchyMultiDex, BothDexClassesAppear)
{
    auto ctx = AnalysisContext::from_dex_files({"tests/data/shapes.dex", "tests/data/classes.dex"});
    ASSERT_TRUE(ctx.has_value()) << ctx.error().message;

    const auto &h = ctx->class_hierarchy();
    EXPECT_TRUE(h.is_loaded("LCircle;"));
    EXPECT_TRUE(h.is_loaded("LTestDex;"));
    // Object is a descriptor-only node referenced from both DEX files.
    EXPECT_FALSE(h.is_loaded("Ljava/lang/Object;"));
    EXPECT_TRUE(h.is_subtype_of("LCircle;", "Ljava/lang/Object;"));
    EXPECT_TRUE(h.is_subtype_of("LTestDex;", "Ljava/lang/Object;"));
}
