// Tests for annotations (Class/Method/Field::annotations) and static-field
// initial values (Field::initial_value), against tests/data/annotated.dex.
//
// Fixture ground truth (Annotated.java):
//   @Marker(name="cls", level=3, tags={"a","b"})  on class Annotated
//   @Marker(name="fld")                           on static final int MAGIC = 42
//   @Marker(name="mth", target=String.class)      on greet()
//   @Deprecated                                   on old()
//   Static finals: MAGIC=42, GREETING="hello", BIG=1234567890123L,
//                  PI=3.25, FLAG=true; `counter` has no recorded value.

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <string>

#include "dex.hpp"

using namespace dex;

namespace {

Class annotated_class()
{
    auto ctx = AnalysisContext::from_dex("tests/data/annotated.dex");
    if (!ctx.has_value())
        throw std::runtime_error(ctx.error().message);
    auto cls = ctx->find_class("LAnnotated;");
    if (!cls.has_value())
        throw std::runtime_error("LAnnotated; not found");
    return *cls; // Class keeps the context alive
}

const Annotation *find_marker(const std::vector<Annotation> &anns)
{
    for (const auto &a : anns)
        if (a.type_descriptor == "LMarker;")
            return &a;
    return nullptr;
}

Method method_named(const Class &cls, std::string_view name)
{
    for (const auto &m : cls.methods())
        if (m.name() == name)
            return m;
    throw std::runtime_error("method not found: " + std::string(name));
}

Field field_named(const Class &cls, std::string_view name)
{
    for (const auto &f : cls.fields())
        if (f.name() == name)
            return f;
    throw std::runtime_error("field not found: " + std::string(name));
}

} // namespace

TEST(AnnotationsTest, ClassAnnotationWithElements)
{
    auto cls = annotated_class();
    auto anns = cls.annotations();

    const Annotation *marker = find_marker(anns);
    ASSERT_NE(marker, nullptr);
    EXPECT_EQ(marker->visibility, AnnotationVisibility::Runtime);

    const AnnotationValue *name = marker->find("name");
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(std::get<std::string>(name->value), "cls");

    const AnnotationValue *level = marker->find("level");
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(std::get<int64_t>(level->value), 3);

    const AnnotationValue *tags = marker->find("tags");
    ASSERT_NE(tags, nullptr);
    const auto &arr = std::get<std::vector<AnnotationValue>>(tags->value);
    ASSERT_EQ(arr.size(), 2u);
    EXPECT_EQ(std::get<std::string>(arr[0].value), "a");
    EXPECT_EQ(std::get<std::string>(arr[1].value), "b");

    // `target` was left at its default — defaults are not materialized.
    EXPECT_EQ(marker->find("target"), nullptr);
}

TEST(AnnotationsTest, MethodAnnotationWithClassLiteral)
{
    auto cls = annotated_class();
    auto anns = method_named(cls, "greet").annotations();

    const Annotation *marker = find_marker(anns);
    ASSERT_NE(marker, nullptr);

    const AnnotationValue *target = marker->find("target");
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(std::get<AnnotationValue::TypeRef>(target->value).descriptor, "Ljava/lang/String;");
}

TEST(AnnotationsTest, DeprecatedOnMethod)
{
    auto cls = annotated_class();
    auto anns = method_named(cls, "old").annotations();

    bool deprecated = std::any_of(anns.begin(), anns.end(), [](const Annotation &a) {
        return a.type_descriptor == "Ljava/lang/Deprecated;";
    });
    EXPECT_TRUE(deprecated);
}

TEST(AnnotationsTest, FieldAnnotation)
{
    auto cls = annotated_class();
    auto anns = field_named(cls, "MAGIC").annotations();

    const Annotation *marker = find_marker(anns);
    ASSERT_NE(marker, nullptr);
    const AnnotationValue *name = marker->find("name");
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(std::get<std::string>(name->value), "fld");
}

TEST(AnnotationsTest, UnannotatedEntitiesReturnEmpty)
{
    auto cls = annotated_class();
    EXPECT_TRUE(field_named(cls, "GREETING").annotations().empty());
    EXPECT_TRUE(method_named(cls, "<init>").annotations().empty());

    // A class with no annotations directory at all.
    auto ctx = AnalysisContext::from_dex("tests/data/classes.dex");
    ASSERT_TRUE(ctx.has_value());
    EXPECT_TRUE(ctx->find_class("LTestDex;")->annotations().empty());
}

TEST(InitialValueTest, AllRecordedKinds)
{
    auto cls = annotated_class();

    auto magic = field_named(cls, "MAGIC").initial_value();
    ASSERT_TRUE(magic.has_value());
    EXPECT_EQ(std::get<int64_t>(magic->value), 42);

    auto greeting = field_named(cls, "GREETING").initial_value();
    ASSERT_TRUE(greeting.has_value());
    EXPECT_EQ(std::get<std::string>(greeting->value), "hello");

    auto big = field_named(cls, "BIG").initial_value();
    ASSERT_TRUE(big.has_value());
    EXPECT_EQ(std::get<int64_t>(big->value), 1234567890123LL);

    auto pi = field_named(cls, "PI").initial_value();
    ASSERT_TRUE(pi.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(pi->value), 3.25);

    auto flag = field_named(cls, "FLAG").initial_value();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(std::get<bool>(flag->value), true);
}

TEST(InitialValueTest, AbsentMeansTypeDefault)
{
    auto cls = annotated_class();
    // `counter` is static but has no recorded initializer.
    EXPECT_FALSE(field_named(cls, "counter").initial_value().has_value());
}

TEST(InitialValueTest, InstanceFieldHasNone)
{
    // Pet.name in polymorphic.dex is an instance field.
    auto ctx = AnalysisContext::from_dex("tests/data/polymorphic.dex");
    ASSERT_TRUE(ctx.has_value());
    auto cls = ctx->find_class("LPet;");
    ASSERT_TRUE(cls.has_value());
    EXPECT_FALSE(field_named(*cls, "name").initial_value().has_value());
}
