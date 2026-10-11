// Holistic raw-parser tests for tests/data/classes.dex.
//
// Ground truth from baksmali:
//   1 class (LTestDex;)  5 method_ids  1 field_id  4 proto_ids  17 strings
//   class_data_off=0x321  <init> code_off=0x168

#include <cstring>
#include <gtest/gtest.h>
#include <span>

#include "raw/parser.hpp"
#include "raw/types.hpp"
#include "test_utils.hpp"

using namespace dex::raw;

TEST(ClassesDex, ParsesHeader)
{
    auto buf = load_buf("tests/data/classes.dex");
    auto result = parser::parse_header(std::span(buf));
    ASSERT_TRUE(result.has_value()) << result.error().message;

    const Header &h = result.value();

    uint8_t expected_magic[8] = {0x64, 0x65, 0x78, 0x0a, 0x30, 0x33, 0x39, 0x00};
    uint8_t expected_signature[20] = {0x39, 0x16, 0x6b, 0xa6, 0x5b, 0xc2, 0x42, 0xe1, 0xdb, 0x24,
                                      0xc4, 0x22, 0x8a, 0x1a, 0x05, 0x00, 0x41, 0x9f, 0xfd, 0x21};

    EXPECT_EQ(std::memcmp(h.magic, expected_magic, 8), 0);
    EXPECT_EQ(h.checksum, 0x76bd94d4u);
    EXPECT_EQ(std::memcmp(h.signature, expected_signature, 20), 0);
    EXPECT_EQ(h.file_size, 980u);
    EXPECT_EQ(h.header_size, 112u);
    EXPECT_EQ(h.endian_tag, 0x12345678u);
    EXPECT_EQ(h.link_size, 0u);
    EXPECT_EQ(h.link_off, 0u);
    EXPECT_EQ(h.map_off, 820u);
    EXPECT_EQ(h.string_ids_size, 17u);
    EXPECT_EQ(h.string_ids_off, 112u);
    EXPECT_EQ(h.type_ids_size, 7u);
    EXPECT_EQ(h.type_ids_off, 180u);
    EXPECT_EQ(h.proto_ids_size, 4u);
    EXPECT_EQ(h.proto_ids_off, 208u);
    EXPECT_EQ(h.field_ids_size, 1u);
    EXPECT_EQ(h.field_ids_off, 256u);
    EXPECT_EQ(h.method_ids_size, 5u);
    EXPECT_EQ(h.method_ids_off, 264u);
    EXPECT_EQ(h.class_defs_size, 1u);
    EXPECT_EQ(h.class_defs_off, 304u);
    EXPECT_EQ(h.data_size, 644u);
    EXPECT_EQ(h.data_off, 336u);
}

TEST(ClassesDex, ParsesMapList)
{
    auto buf = load_buf("tests/data/classes.dex");
    auto header = parser::parse_header(std::span(buf));
    ASSERT_TRUE(header.has_value());
    auto result = parser::parse_map_list(std::span(buf), header->map_off);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    const MapList &ml = result.value();
    EXPECT_GT(ml.size, 0u);
    EXPECT_EQ(ml.items.size(), ml.size);
    EXPECT_EQ(ml.items[0].type, 0x0000u);
    EXPECT_EQ(ml.items[0].size, 1u);
    EXPECT_EQ(ml.items[0].offset, 0u);

    uint32_t prev = 0;
    for (const auto &item : ml.items) {
        EXPECT_GE(item.offset, prev);
        prev = item.offset;
    }
}

TEST(ClassesDex, ParsesStringIds)
{
    auto buf = load_buf("tests/data/classes.dex");
    auto header = parser::parse_header(std::span(buf));
    ASSERT_TRUE(header.has_value());

    auto result =
        parser::parse_string_ids(std::span(buf), header->string_ids_off, header->string_ids_size);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    const auto &ids = result.value();
    EXPECT_EQ(ids.size(), 17u);
    EXPECT_EQ(ids[0], 0x1c2u);  // "<init>"
    EXPECT_EQ(ids[1], 0x1cau);  // "Hello Dex++!!"
    EXPECT_EQ(ids[2], 0x1d9u);  // "L"
    EXPECT_EQ(ids[3], 0x1dcu);  // "LTestDex;"
    EXPECT_EQ(ids[4], 0x1e7u);  // "Ljava/io/PrintStream;"
    EXPECT_EQ(ids[5], 0x1feu);  // "Ljava/lang/Object;"
    EXPECT_EQ(ids[6], 0x212u);  // "Ljava/lang/String;"
    EXPECT_EQ(ids[7], 0x226u);  // "Ljava/lang/System;"
    EXPECT_EQ(ids[8], 0x23au);  // "TestDex.java"
    EXPECT_EQ(ids[9], 0x248u);  // "V"
    EXPECT_EQ(ids[10], 0x24bu); // "VL"
    EXPECT_EQ(ids[11], 0x24fu); // "[Ljava/lang/String;"
    EXPECT_EQ(ids[12], 0x264u); // "helloDex"
    EXPECT_EQ(ids[13], 0x26eu); // "main"
    EXPECT_EQ(ids[14], 0x274u); // "out"
    EXPECT_EQ(ids[15], 0x279u); // "println"
    EXPECT_EQ(ids[16], 0x282u); // D8 metadata
}

TEST(ClassesDex, ParsesTypeIds)
{
    auto buf = load_buf("tests/data/classes.dex");
    auto header = parser::parse_header(std::span(buf));
    ASSERT_TRUE(header.has_value());

    auto result =
        parser::parse_type_ids(std::span(buf), header->type_ids_off, header->type_ids_size);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    const auto &ids = result.value();
    EXPECT_EQ(ids.size(), 7u);
    EXPECT_EQ(ids[0], 3u);  // "LTestDex;"
    EXPECT_EQ(ids[1], 4u);  // "Ljava/lang/String;"
    EXPECT_EQ(ids[2], 5u);  // "V"
    EXPECT_EQ(ids[3], 6u);  // "VL"
    EXPECT_EQ(ids[4], 7u);  // "[Ljava/lang/String;"
    EXPECT_EQ(ids[5], 9u);  // "Ljava/io/PrintStream;"
    EXPECT_EQ(ids[6], 11u); // "Ljava/lang/System;"
}

TEST(ClassesDex, ParsesProtoIds)
{
    auto buf = load_buf("tests/data/classes.dex");
    auto header = parser::parse_header(std::span(buf));
    ASSERT_TRUE(header.has_value());

    auto result =
        parser::parse_proto_ids(std::span(buf), header->proto_ids_off, header->proto_ids_size);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    const auto &ids = result.value();
    EXPECT_EQ(ids.size(), 4u);
    EXPECT_EQ(ids[0].shorty_idx, 2u);
    EXPECT_EQ(ids[0].return_type_idx, 3u);
    EXPECT_EQ(ids[0].parameters_off, 0u);
    EXPECT_EQ(ids[1].shorty_idx, 9u);
    EXPECT_EQ(ids[1].return_type_idx, 5u);
    EXPECT_EQ(ids[1].parameters_off, 0u);
    EXPECT_EQ(ids[2].shorty_idx, 10u);
    EXPECT_EQ(ids[2].return_type_idx, 5u);
    EXPECT_EQ(ids[2].parameters_off, 436u);
    EXPECT_EQ(ids[3].shorty_idx, 10u);
    EXPECT_EQ(ids[3].return_type_idx, 5u);
    EXPECT_EQ(ids[3].parameters_off, 444u);
}

TEST(ClassesDex, ParsesFieldIds)
{
    auto buf = load_buf("tests/data/classes.dex");
    auto header = parser::parse_header(std::span(buf));
    ASSERT_TRUE(header.has_value());

    auto result =
        parser::parse_field_ids(std::span(buf), header->field_ids_off, header->field_ids_size);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    const auto &ids = result.value();
    EXPECT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids[0].class_idx, 4u); // Ljava/lang/System;
    EXPECT_EQ(ids[0].type_idx, 1u);  // Ljava/io/PrintStream;
    EXPECT_EQ(ids[0].name_idx, 14u); // "out"
}

TEST(ClassesDex, ParsesMethodIds)
{
    auto buf = load_buf("tests/data/classes.dex");
    auto header = parser::parse_header(std::span(buf));
    ASSERT_TRUE(header.has_value());

    auto result =
        parser::parse_method_ids(std::span(buf), header->method_ids_off, header->method_ids_size);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    const auto &ids = result.value();
    EXPECT_EQ(ids.size(), 5u);
    EXPECT_EQ(ids[0].class_idx, 0u);
    EXPECT_EQ(ids[0].proto_idx, 1u);
    EXPECT_EQ(ids[0].name_idx, 0u);
    EXPECT_EQ(ids[1].class_idx, 0u);
    EXPECT_EQ(ids[1].proto_idx, 0u);
    EXPECT_EQ(ids[1].name_idx, 12u);
    EXPECT_EQ(ids[2].class_idx, 0u);
    EXPECT_EQ(ids[2].proto_idx, 3u);
    EXPECT_EQ(ids[2].name_idx, 13u);
    EXPECT_EQ(ids[3].class_idx, 1u);
    EXPECT_EQ(ids[3].proto_idx, 2u);
    EXPECT_EQ(ids[3].name_idx, 15u);
    EXPECT_EQ(ids[4].class_idx, 2u);
    EXPECT_EQ(ids[4].proto_idx, 1u);
    EXPECT_EQ(ids[4].name_idx, 0u);
}

TEST(ClassesDex, ParsesClassDefs)
{
    auto buf = load_buf("tests/data/classes.dex");
    auto header = parser::parse_header(std::span(buf));
    ASSERT_TRUE(header.has_value());

    auto result =
        parser::parse_class_defs(std::span(buf), header->class_defs_off, header->class_defs_size);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    const auto &defs = result.value();
    ASSERT_EQ(defs.size(), 1u);
    EXPECT_EQ(defs[0].class_idx, 0u);
    EXPECT_EQ(defs[0].access_flags, 0x1u);
    EXPECT_EQ(defs[0].superclass_idx, 2u);
    EXPECT_EQ(defs[0].interfaces_off, 0u);
    EXPECT_EQ(defs[0].source_file_idx, 8u);
    EXPECT_EQ(defs[0].annotations_off, 0u);
    EXPECT_EQ(defs[0].class_data_off, 0x321u);
    EXPECT_EQ(defs[0].static_values_off, 0u);
}

TEST(ClassesDex, ParsesCallSiteAndMethodHandlesEmpty)
{
    auto buf = load_buf("tests/data/classes.dex");
    auto header = parser::parse_header(std::span(buf));
    ASSERT_TRUE(header.has_value());

    auto cs = parser::parse_call_site_ids(std::span(buf), header->class_defs_off, 0);
    ASSERT_TRUE(cs.has_value());
    EXPECT_TRUE(cs->empty());

    auto mh = parser::parse_method_handles(std::span(buf), header->class_defs_off, 0);
    ASSERT_TRUE(mh.has_value());
    EXPECT_TRUE(mh->empty());
}

TEST(ClassesDex, ParsesSingleString)
{
    auto buf = load_buf("tests/data/classes.dex");
    auto result = parser::parse_string(std::span(buf), 0x1ca);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->first, "Hello Dex++!!");
    EXPECT_EQ(result->second, 0x1ca + 15u);
}

TEST(ClassesDex, ParsesAllStrings)
{
    auto buf = load_buf("tests/data/classes.dex");
    auto header = parser::parse_header(std::span(buf));
    ASSERT_TRUE(header.has_value());

    auto ids_result =
        parser::parse_string_ids(std::span(buf), header->string_ids_off, header->string_ids_size);
    ASSERT_TRUE(ids_result.has_value());

    auto result = parser::parse_strings(std::span(buf), *ids_result);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    const auto &strings = result.value();
    EXPECT_EQ(strings.size(), 17u);
    EXPECT_EQ(strings[0], "<init>");
    EXPECT_EQ(strings[1], "Hello Dex++!!");
    EXPECT_EQ(strings[3], "LTestDex;");
    EXPECT_EQ(strings[12], "helloDex");
    EXPECT_EQ(strings[13], "main");
}

TEST(ClassesDex, ParsesTypeList)
{
    auto buf = load_buf("tests/data/classes.dex");

    // proto_ids[2].parameters_off = 436 → 1 entry (Ljava/lang/String;)
    auto r1 = parser::parse_type_list(std::span(buf), 436);
    ASSERT_TRUE(r1.has_value()) << r1.error().message;
    EXPECT_EQ(r1->size, 1u);
    ASSERT_EQ(r1->type_ids.size(), 1u);
    EXPECT_EQ(r1->type_ids[0], 3u);

    // proto_ids[3].parameters_off = 444 → 1 entry ([Ljava/lang/String;)
    auto r2 = parser::parse_type_list(std::span(buf), 444);
    ASSERT_TRUE(r2.has_value()) << r2.error().message;
    EXPECT_EQ(r2->size, 1u);
    ASSERT_EQ(r2->type_ids.size(), 1u);
    EXPECT_EQ(r2->type_ids[0], 6u);
}

TEST(ClassesDex, ParsesClassData)
{
    auto buf = load_buf("tests/data/classes.dex");
    auto result = parser::parse_class_data(std::span(buf), 0x321);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    const ClassDataItem &cd = result.value();
    EXPECT_EQ(cd.static_fields.size(), 0u);
    EXPECT_EQ(cd.instance_fields.size(), 0u);
    EXPECT_EQ(cd.direct_methods.size(), 3u);
    EXPECT_EQ(cd.virtual_methods.size(), 0u);

    EXPECT_EQ(cd.direct_methods[0].method_idx, 0u);
    EXPECT_EQ(cd.direct_methods[0].access_flags, 0x10001u);
    EXPECT_EQ(cd.direct_methods[0].code_off, 0x168u);

    EXPECT_EQ(cd.direct_methods[1].method_idx, 1u);
    EXPECT_EQ(cd.direct_methods[1].access_flags, 0x9u);
    EXPECT_EQ(cd.direct_methods[1].code_off, 0x150u);

    EXPECT_EQ(cd.direct_methods[2].method_idx, 2u);
    EXPECT_EQ(cd.direct_methods[2].access_flags, 0x9u);
    EXPECT_EQ(cd.direct_methods[2].code_off, 0x180u);
}

TEST(ClassesDex, ParsesCodeItem)
{
    auto buf = load_buf("tests/data/classes.dex");
    auto result = parser::parse_code_item(std::span(buf), 0x168); // <init>
    ASSERT_TRUE(result.has_value()) << result.error().message;

    const CodeItem &ci = result.value();
    EXPECT_EQ(ci.registers_size, 1u);
    EXPECT_EQ(ci.ins_size, 1u);
    EXPECT_EQ(ci.outs_size, 1u);
    EXPECT_EQ(ci.tries_size, 0u);
    EXPECT_EQ(ci.debug_info_off, 0x1a8u);
    EXPECT_EQ(ci.insns_size, 4u);
    EXPECT_EQ(ci.insns.size(), 4u);
    EXPECT_TRUE(ci.tries.empty());
    EXPECT_TRUE(ci.handlers.empty());
}

TEST(ClassesDex, ParsesFullBuffer)
{
    auto buf = load_buf("tests/data/classes.dex");
    auto result = parser::parse_buffer(std::span(buf));
    ASSERT_TRUE(result.has_value()) << result.error().message;

    const DexFile &dex = result.value();
    EXPECT_EQ(dex.strings().size(), 17u);
    EXPECT_EQ(dex.type_ids().size(), 7u);
    EXPECT_EQ(dex.proto_ids().size(), 4u);
    EXPECT_EQ(dex.field_ids().size(), 1u);
    EXPECT_EQ(dex.method_ids().size(), 5u);
    EXPECT_EQ(dex.class_defs().size(), 1u);
    EXPECT_TRUE(dex.call_site_ids().empty());
    EXPECT_TRUE(dex.method_handles().empty());
    EXPECT_EQ(dex.strings()[0], "<init>");
    EXPECT_EQ(dex.strings()[1], "Hello Dex++!!");
    EXPECT_EQ(dex.strings()[13], "main");
    EXPECT_EQ(dex.type_ids()[0], 3u);
    EXPECT_EQ(dex.class_defs()[0].class_idx, 0u);
    EXPECT_EQ(dex.header().file_size, 980u);
    EXPECT_GT(dex.map_list().size, 0u);
}

TEST(ClassesDex, ParsesFullFile)
{
    auto result = parser::parse_file("tests/data/classes.dex");
    ASSERT_TRUE(result.has_value()) << result.error().message;

    const DexFile &dex = result.value();
    EXPECT_EQ(dex.strings().size(), 17u);
    EXPECT_EQ(dex.class_defs().size(), 1u);
    EXPECT_EQ(dex.strings()[0], "<init>");
    EXPECT_EQ(dex.strings()[1], "Hello Dex++!!");
}

TEST(ClassesDex, ParseFileReturnsErrorForMissingFile)
{
    auto result = parser::parse_file("tests/data/nonexistent.dex");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ParseError::Code::NullPointer);
}
