#include <gtest/gtest.h>

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "dex.hpp"

using namespace dex;

// Class, Method, Field and ClassRef compare equal when they come from the
// same analysis and point at the same thing, and std::hash agrees with ==.

namespace {

constexpr const char *SHAPES = "tests/data/shapes.dex";

Class class_named(const AnalysisContext &ctx, std::string_view descriptor)
{
    std::optional<Class> cls = ctx.find_class(descriptor);
    if (!cls.has_value())
        throw std::runtime_error("no class " + std::string(descriptor));
    return *cls;
}

template <typename T> std::size_t hash_of(const T &value) { return std::hash<T>{}(value); }

} // namespace

TEST(HandleEqualityTest, SameClassFromTwoLookupsIsEqual)
{
    auto ctx = AnalysisContext::from_dex(SHAPES).value();
    Class a = class_named(ctx, "LCircle;");
    Class b = class_named(ctx, "LCircle;");
    EXPECT_EQ(a, b);
    EXPECT_EQ(hash_of(a), hash_of(b));

    // The handle in classes() is the same class as the one find_class gives.
    std::vector<Class> all = ctx.classes();
    std::size_t found = 0;
    for (const Class &cls : all)
        found += cls == a ? 1 : 0;
    EXPECT_EQ(found, 1u);

    EXPECT_NE(a, class_named(ctx, "LRectangle;"));
}

TEST(HandleEqualityTest, ResolveGivesTheClassFindClassGives)
{
    auto ctx = AnalysisContext::from_dex(SHAPES).value();
    ClassRef shape = class_named(ctx, "LAbstractShape;").interfaces().front();
    EXPECT_EQ(shape.resolve(), std::optional<Class>(class_named(ctx, "LShape;")));

    // resolve() gives nullopt for a class that is not loaded.
    ClassRef object = class_named(ctx, "LShape;").superclass().value();
    EXPECT_FALSE(object.resolve().has_value());
}

TEST(HandleEqualityTest, CopiesOfAContextShareItsHandles)
{
    auto ctx = AnalysisContext::from_dex(SHAPES).value();
    AnalysisContext copy = ctx;
    EXPECT_EQ(class_named(copy, "LCircle;"), class_named(ctx, "LCircle;"));
    EXPECT_EQ(copy.find_class("LCircle;")->methods(), ctx.find_class("LCircle;")->methods());
}

TEST(HandleEqualityTest, HandlesFromSeparateLoadsDiffer)
{
    auto first = AnalysisContext::from_dex(SHAPES).value();
    auto second = AnalysisContext::from_dex(SHAPES).value();
    Class a = class_named(first, "LCircle;");
    Class b = class_named(second, "LCircle;");
    EXPECT_NE(a, b);
    EXPECT_NE(a.methods().front(), b.methods().front());
    EXPECT_NE(a.fields().front(), b.fields().front());
    EXPECT_NE(a.superclass().value(), b.superclass().value());
}

TEST(HandleEqualityTest, MethodsCompareByMethod)
{
    auto ctx = AnalysisContext::from_dex(SHAPES).value();
    Class circle = class_named(ctx, "LCircle;");
    std::vector<Method> once = circle.methods();
    std::vector<Method> again = circle.methods();
    ASSERT_GE(once.size(), 2u);
    ASSERT_EQ(once.size(), again.size());
    for (std::size_t i = 0; i < once.size(); ++i) {
        EXPECT_EQ(once[i], again[i]);
        EXPECT_EQ(hash_of(once[i]), hash_of(again[i]));
    }
    EXPECT_NE(once[0], once[1]);

    std::unordered_set<Method> unique(once.begin(), once.end());
    unique.insert(again.begin(), again.end());
    EXPECT_EQ(unique.size(), once.size());
}

TEST(HandleEqualityTest, FieldsCompareByField)
{
    auto ctx = AnalysisContext::from_dex(SHAPES).value();
    std::vector<Field> once = class_named(ctx, "LRectangle;").fields();
    std::vector<Field> again = class_named(ctx, "LRectangle;").fields();
    ASSERT_GE(once.size(), 2u);
    EXPECT_EQ(once, again);
    EXPECT_EQ(hash_of(once[0]), hash_of(again[0]));
    EXPECT_NE(once[0], once[1]);

    std::unordered_set<Field> unique(once.begin(), once.end());
    unique.insert(again.begin(), again.end());
    EXPECT_EQ(unique.size(), once.size());

    // A field and the class that declares it: the field of Circle is not one
    // of Rectangle's.
    Field radius = class_named(ctx, "LCircle;").fields().front();
    EXPECT_EQ(unique.count(radius), 0u);
}

TEST(HandleEqualityTest, ClassRefsCompareByDescriptor)
{
    auto ctx = AnalysisContext::from_dex(SHAPES).value();
    ClassRef from_circle = class_named(ctx, "LCircle;").superclass().value();
    ClassRef from_rectangle = class_named(ctx, "LRectangle;").superclass().value();
    EXPECT_EQ(from_circle.descriptor(), "LAbstractShape;");
    EXPECT_EQ(from_circle, from_rectangle);
    EXPECT_EQ(hash_of(from_circle), hash_of(from_rectangle));

    ClassRef shape = class_named(ctx, "LAbstractShape;").interfaces().front();
    EXPECT_EQ(shape.descriptor(), "LShape;");
    EXPECT_NE(from_circle, shape);

    std::unordered_set<ClassRef> unique{from_circle, from_rectangle, shape};
    EXPECT_EQ(unique.size(), 2u);
}

TEST(HandleEqualityTest, HandlesStayEqualAfterTheContextIsGone)
{
    std::optional<Class> a;
    std::optional<Class> b;
    {
        auto ctx = AnalysisContext::from_dex(SHAPES).value();
        a = class_named(ctx, "LCircle;");
        b = class_named(ctx, "LCircle;");
    }
    ASSERT_TRUE(a.has_value() && b.has_value());
    EXPECT_EQ(*a, *b);
    EXPECT_EQ(a->methods(), b->methods());
}
