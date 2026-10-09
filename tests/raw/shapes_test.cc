// Holistic raw-parser tests for tests/data/shapes.dex.
//
// Ground truth from baksmali:
//   class_defs order:
//     [0] LShape;         access_flags=0x0600  class_data_off=0x50a
//     [1] LAbstractShape; access_flags=0x0400  interfaces_off=0x380  class_data_off=0x516
//     [2] LCircle;        access_flags=0x0000  class_data_off=0x526
//     [3] LRectangle;     access_flags=0x0000  class_data_off=0x53e
//   field_ids: [0]=color(String) [1]=radius(D) [2]=height(D) [3]=width(D)

#include <gtest/gtest.h>
#include <span>

#include "raw/parser.hpp"
#include "raw/types.hpp"
#include "test_utils.hpp"

using namespace dex::raw;

TEST(ShapesDex, FourClassDefs)
{
    auto dex = parser::parse_file("tests/data/shapes.dex");
    ASSERT_TRUE(dex.has_value()) << dex.error().message;
    EXPECT_EQ(dex->class_defs().size(), 4u);
}

TEST(ShapesDex, FourFieldIds)
{
    auto dex = parser::parse_file("tests/data/shapes.dex");
    ASSERT_TRUE(dex.has_value()) << dex.error().message;
    EXPECT_EQ(dex->field_ids().size(), 4u);
}

TEST(ShapesDex, ShapeAccessFlagsInterfaceAndAbstract)
{
    auto dex = parser::parse_file("tests/data/shapes.dex");
    ASSERT_TRUE(dex.has_value()) << dex.error().message;
    EXPECT_EQ(dex->class_defs()[0].access_flags, 0x0600u);
}

TEST(ShapesDex, AbstractShapeAccessFlagsAbstractOnly)
{
    auto dex = parser::parse_file("tests/data/shapes.dex");
    ASSERT_TRUE(dex.has_value()) << dex.error().message;
    EXPECT_EQ(dex->class_defs()[1].access_flags, 0x0400u);
}

TEST(ShapesDex, CircleAndRectangleAccessFlagsZero)
{
    auto dex = parser::parse_file("tests/data/shapes.dex");
    ASSERT_TRUE(dex.has_value()) << dex.error().message;
    EXPECT_EQ(dex->class_defs()[2].access_flags, 0x0u);
    EXPECT_EQ(dex->class_defs()[3].access_flags, 0x0u);
}

TEST(ShapesDex, AbstractShapeImplementsOneInterface)
{
    auto buf = load_buf("tests/data/shapes.dex");
    auto dex = parser::parse_buffer(std::span(buf));
    ASSERT_TRUE(dex.has_value()) << dex.error().message;

    EXPECT_NE(dex->class_defs()[1].interfaces_off, 0u);

    auto ifaces = parser::parse_type_list(std::span(buf), dex->class_defs()[1].interfaces_off);
    ASSERT_TRUE(ifaces.has_value()) << ifaces.error().message;
    EXPECT_EQ(ifaces->size, 1u);
    EXPECT_EQ(ifaces->type_ids[0], 4u); // type_ids[4] = LShape;
}

TEST(ShapesDex, AbstractShapeClassDataOneProtectedInstanceField)
{
    auto buf = load_buf("tests/data/shapes.dex");
    auto cd = parser::parse_class_data(std::span(buf), 0x516);
    ASSERT_TRUE(cd.has_value()) << cd.error().message;
    EXPECT_EQ(cd->static_fields.size(), 0u);
    EXPECT_EQ(cd->instance_fields.size(), 1u);
    EXPECT_EQ(cd->direct_methods.size(), 1u);
    EXPECT_EQ(cd->virtual_methods.size(), 1u);
    EXPECT_EQ(cd->instance_fields[0].access_flags, 0x4u); // protected
}

TEST(ShapesDex, CircleClassDataOnePrivateInstanceField)
{
    auto buf = load_buf("tests/data/shapes.dex");
    auto cd = parser::parse_class_data(std::span(buf), 0x526);
    ASSERT_TRUE(cd.has_value()) << cd.error().message;
    EXPECT_EQ(cd->instance_fields.size(), 1u);
    EXPECT_EQ(cd->direct_methods.size(), 1u);
    EXPECT_EQ(cd->virtual_methods.size(), 3u);
    EXPECT_EQ(cd->instance_fields[0].access_flags, 0x2u); // private
}

TEST(ShapesDex, RectangleClassDataTwoPrivateInstanceFields)
{
    auto buf = load_buf("tests/data/shapes.dex");
    auto cd = parser::parse_class_data(std::span(buf), 0x53e);
    ASSERT_TRUE(cd.has_value()) << cd.error().message;
    EXPECT_EQ(cd->instance_fields.size(), 2u);
    EXPECT_EQ(cd->direct_methods.size(), 1u);
    EXPECT_EQ(cd->virtual_methods.size(), 2u);
    EXPECT_EQ(cd->instance_fields[0].access_flags, 0x2u); // private (height)
    EXPECT_EQ(cd->instance_fields[1].access_flags, 0x2u); // private (width)
}
