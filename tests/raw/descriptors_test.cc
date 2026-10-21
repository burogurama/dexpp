// Names and type descriptors (raw/descriptors.hpp).

#include <gtest/gtest.h>

#include <string>

#include "raw/descriptors.hpp"

using namespace dex::raw;

TEST(Descriptors, SimpleNames)
{
    EXPECT_TRUE(is_valid_simple_name("abc$_-09", 35));
    EXPECT_TRUE(is_valid_simple_name("\xC3\xA9t\xC3\xA9", 35)); // été
    EXPECT_TRUE(is_valid_simple_name("\xF0\x9F\x98\x80", 35));  // supplementary character
    EXPECT_FALSE(is_valid_simple_name("", 35));
    EXPECT_FALSE(is_valid_simple_name("a.b", 35));
    EXPECT_FALSE(is_valid_simple_name("a;b", 35));
    EXPECT_FALSE(is_valid_simple_name("\xED\xA0\x80", 35)); // lone surrogate
    EXPECT_FALSE(is_valid_simple_name("\xC2\x85", 35));     // C1 control
    // Space characters only from version 040.
    EXPECT_FALSE(is_valid_simple_name("a b", 39));
    EXPECT_TRUE(is_valid_simple_name("a b", 40));
    EXPECT_FALSE(is_valid_simple_name("a\u00A0b", 39)); // no-break space
    EXPECT_TRUE(is_valid_simple_name("a\u00A0b", 40));
    EXPECT_FALSE(is_valid_simple_name("a\u200Bb", 40)); // zero-width space: a format char
}

TEST(Descriptors, MemberNames)
{
    EXPECT_TRUE(is_valid_member_name("<init>", 35));
    EXPECT_TRUE(is_valid_member_name("<clinit>", 35));
    EXPECT_TRUE(is_valid_member_name("<anything>", 35)); // the syntax allows it
    EXPECT_FALSE(is_valid_member_name("<>", 35));
    EXPECT_FALSE(is_valid_member_name("<init", 35));
    EXPECT_TRUE(is_valid_member_name("run", 35));
}

TEST(Descriptors, TypeDescriptors)
{
    for (const char *ok :
         {"I", "V", "J", "Ljava/lang/String;", "[I", "[[Ljava/lang/Object;", "La;"})
        EXPECT_TRUE(is_valid_type_descriptor(ok, 35)) << ok;
    for (const char *bad :
         {"", "X", "[V", "L;", "Ljava//String;", "Ljava/lang/String", "Lfoo/;", "[", "LA;B"})
        EXPECT_FALSE(is_valid_type_descriptor(bad, 35)) << bad;
    std::string deep(255, '[');
    EXPECT_TRUE(is_valid_type_descriptor(deep + "I", 35));
    EXPECT_FALSE(is_valid_type_descriptor(deep + "[I", 35));
    EXPECT_TRUE(is_valid_class_descriptor("La/B;", 35));
    EXPECT_FALSE(is_valid_class_descriptor("[La/B;", 35));
    EXPECT_FALSE(is_valid_field_type("V", 35));
    EXPECT_TRUE(is_valid_field_type("Z", 35));
}

TEST(Descriptors, Prototypes)
{
    auto parts = parse_proto("(I[Ljava/lang/String;J)V");
    ASSERT_TRUE(parts.has_value());
    ASSERT_EQ(parts->params.size(), 3u);
    EXPECT_EQ(parts->params[0], "I");
    EXPECT_EQ(parts->params[1], "[Ljava/lang/String;");
    EXPECT_EQ(parts->params[2], "J");
    EXPECT_EQ(parts->return_type, "V");

    EXPECT_TRUE(is_valid_proto("()V", 35));
    EXPECT_TRUE(is_valid_proto("()[I", 35));
    EXPECT_FALSE(is_valid_proto("(V)V", 35)); // void parameter
    EXPECT_FALSE(is_valid_proto("()", 35));
    EXPECT_FALSE(is_valid_proto("(I", 35));
    EXPECT_FALSE(is_valid_proto("I)V", 35));
    EXPECT_FALSE(is_valid_proto("()II", 35));

    EXPECT_EQ(make_proto({"I", "Ljava/lang/String;"}, "V"), "(ILjava/lang/String;)V");
    EXPECT_EQ(shorty_of("(I[JLjava/lang/String;D)Z"), "ZILLD");
    EXPECT_EQ(shorty_of("()V"), "V");
    EXPECT_EQ(parameter_words("(IJDLjava/lang/Object;)V"), 6u);
}

TEST(Descriptors, JavaNames)
{
    EXPECT_EQ(descriptor_of_class("java.lang.String"), "Ljava/lang/String;");
    EXPECT_EQ(descriptor_of_class("a.Outer$Inner"), "La/Outer$Inner;");
    EXPECT_EQ(java_name_of("Ljava/lang/String;"), "java.lang.String");
    EXPECT_EQ(java_name_of("I"), "int");
    EXPECT_EQ(java_name_of("[Ljava/lang/String;"), "[Ljava.lang.String;");
}
