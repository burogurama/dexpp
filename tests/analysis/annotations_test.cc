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
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "../raw/test_utils.hpp"
#include "../raw/zip_builder.hpp"
#include "dex.hpp"
#include "raw/constants.hpp"
#include "raw/parser.hpp"

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
    EXPECT_EQ(std::get<AnnotationValue::TypeValue>(target->value).descriptor, "Ljava/lang/String;");
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

// ---- Finding a member's class_def in a damaged file ----
//
// Method::annotations(), Field::annotations() and Field::initial_value() find
// the class_def of the member's class by the class's type index.  These tests
// patch annotated.dex in memory so that lookup meets a damaged class_defs
// table, and load the result as the only DEX of an in-memory APK (nothing is
// added to tests/data, where the model round-trip test would read it).

namespace {

// annotated.dex and where its tables are, read with the raw parser.
struct AnnotatedDex
{
    std::vector<uint8_t> bytes;
    std::size_t class_defs_off = 0;
    std::size_t method_ids_off = 0;
    std::size_t field_ids_off = 0;
    std::size_t method_count = 0;
    std::size_t field_count = 0;
    uint32_t type_count = 0;
    uint32_t annotated_type = 0; ///< type index of LAnnotated;
    uint32_t annotated_def = 0;  ///< class_defs index of LAnnotated;
    uint32_t marker_def = 0;     ///< class_defs index of LMarker;
};

AnnotatedDex annotated_dex()
{
    AnnotatedDex d;
    d.bytes = load_buf("tests/data/annotated.dex");
    auto dex = raw::parser::parse_buffer(d.bytes);
    if (!dex.has_value())
        throw std::runtime_error(dex.error().message);
    d.class_defs_off = dex->header().class_defs_off;
    d.method_ids_off = dex->header().method_ids_off;
    d.field_ids_off = dex->header().field_ids_off;
    d.method_count = dex->method_ids().size();
    d.field_count = dex->field_ids().size();
    d.type_count = static_cast<uint32_t>(dex->type_ids().size());
    const auto defs = dex->class_defs();
    for (uint32_t j = 0; j < static_cast<uint32_t>(defs.size()); ++j) {
        const std::string &name = dex->strings()[dex->type_ids()[defs[j].class_idx]];
        if (name == "LAnnotated;") {
            d.annotated_def = j;
            d.annotated_type = defs[j].class_idx;
        }
        else if (name == "LMarker;") {
            d.marker_def = j;
        }
    }
    return d;
}

void put_u16(std::vector<uint8_t> &bytes, std::size_t off, uint16_t value)
{
    bytes[off] = static_cast<uint8_t>(value);
    bytes[off + 1] = static_cast<uint8_t>(value >> 8);
}

void put_u32(std::vector<uint8_t> &bytes, std::size_t off, uint32_t value)
{
    put_u16(bytes, off, static_cast<uint16_t>(value));
    put_u16(bytes, off + 2, static_cast<uint16_t>(value >> 16));
}

constexpr std::size_t kClassDefSize = 32; // class_def_item; class_idx is its first field
constexpr std::size_t kMemberIdSize = 8;  // method_id_item / field_id_item; class_idx first

void set_class_def_type(AnnotatedDex &d, uint32_t def, uint32_t type)
{
    put_u32(d.bytes, d.class_defs_off + def * kClassDefSize, type);
}

void swap_class_defs(AnnotatedDex &d, uint32_t a, uint32_t b)
{
    auto at = [&](uint32_t def) {
        return d.bytes.begin() +
               static_cast<std::ptrdiff_t>(d.class_defs_off + def * kClassDefSize);
    };
    std::swap_ranges(at(a), at(a) + static_cast<std::ptrdiff_t>(kClassDefSize), at(b));
}

// Make every method_id and field_id that names type @p from name @p to.
void move_members_to_type(AnnotatedDex &d, uint16_t from, uint16_t to)
{
    for (std::size_t i = 0; i < d.method_count; ++i) {
        std::size_t off = d.method_ids_off + i * kMemberIdSize;
        if ((d.bytes[off] | d.bytes[off + 1] << 8) == from)
            put_u16(d.bytes, off, to);
    }
    for (std::size_t i = 0; i < d.field_count; ++i) {
        std::size_t off = d.field_ids_off + i * kMemberIdSize;
        if ((d.bytes[off] | d.bytes[off + 1] << 8) == from)
            put_u16(d.bytes, off, to);
    }
}

AnalysisContext load_dex_bytes(const std::vector<uint8_t> &dex_bytes)
{
    auto ctx =
        AnalysisContext::from_apk_buffer(build_zip({stored_entry("classes.dex", dex_bytes)}));
    if (!ctx.has_value())
        throw std::runtime_error(ctx.error().message);
    return std::move(*ctx);
}

// The class whose class_data defines a method named @p method.  After the
// patches, find_class by name may give another class_def.
Class class_defining(const AnalysisContext &ctx, std::string_view method)
{
    for (const auto &c : ctx.classes())
        for (const auto &m : c.methods())
            if (m.name() == method)
                return c;
    throw std::runtime_error("no class defines " + std::string(method));
}

// What the three lookups give for Annotated's members.
struct Seen
{
    bool greet_marker = false; ///< greet() has its @Marker
    bool magic_marker = false; ///< MAGIC has its @Marker
    bool magic_value = false;  ///< MAGIC has its initial value 42
};

Seen seen_in(const std::vector<uint8_t> &dex_bytes)
{
    auto ctx = load_dex_bytes(dex_bytes);
    Class cls = class_defining(ctx, "greet");
    Seen s;
    s.greet_marker = find_marker(method_named(cls, "greet").annotations()) != nullptr;
    s.magic_marker = find_marker(field_named(cls, "MAGIC").annotations()) != nullptr;
    auto value = field_named(cls, "MAGIC").initial_value();
    s.magic_value = value.has_value() && std::get<int64_t>(value->value) == 42;
    return s;
}

} // namespace

// ---- Method handle and method type values (handles.dex) ----
//
// handles.dex (built by tests/model/handles_fixture_test.cc) annotates its
// class with @HandleMarker(getter = <static-get Handles.counter>,
// invoker = <invoke-static Handles.target(I)I>, type = <method type (I)I>).

namespace {

// The value of element @p name of @HandleMarker; a null value when it is missing.
AnnotationValue handle_marker_element(const AnalysisContext &ctx, std::string_view name)
{
    auto cls = ctx.find_class("LHandles;");
    if (!cls.has_value())
        throw std::runtime_error("LHandles; not found");
    for (const auto &a : cls->annotations())
        if (a.type_descriptor == "LHandleMarker;")
            if (const AnnotationValue *v = a.find(name))
                return *v;
    return AnnotationValue{};
}

} // namespace

TEST(AnnotationsTest, MethodHandleAndMethodTypeValues)
{
    auto ctx = AnalysisContext::from_dex("tests/data/handles.dex");
    ASSERT_TRUE(ctx.has_value()) << ctx.error().message;

    using Handle = AnnotationValue::MethodHandleValue;
    const auto getter = handle_marker_element(*ctx, "getter").value;
    ASSERT_TRUE(std::holds_alternative<Handle>(getter));
    EXPECT_EQ(std::get<Handle>(getter),
              (Handle{MethodHandleKind::StaticGet, FieldId{"LHandles;", "counter", "I"}}));

    const auto invoker = handle_marker_element(*ctx, "invoker").value;
    ASSERT_TRUE(std::holds_alternative<Handle>(invoker));
    EXPECT_EQ(std::get<Handle>(invoker),
              (Handle{MethodHandleKind::InvokeStatic, MethodId{"LHandles;", "target", "(I)I"}}));

    const auto type = handle_marker_element(*ctx, "type").value;
    ASSERT_TRUE(std::holds_alternative<AnnotationValue::MethodTypeValue>(type));
    EXPECT_EQ(std::get<AnnotationValue::MethodTypeValue>(type).proto, "(I)I");
}

TEST(AnnotationsTest, DamagedMethodHandleGivesAnEmptyMember)
{
    // Give every method handle a type the format does not define (9).
    auto bytes = load_buf("tests/data/handles.dex");
    auto dex = raw::parser::parse_buffer(bytes);
    ASSERT_TRUE(dex.has_value()) << dex.error().message;
    std::size_t patched = 0;
    for (const raw::MapItem &item : dex->map_list().items) {
        if (item.type != raw::TYPE_METHOD_HANDLE_ITEM)
            continue;
        for (uint32_t i = 0; i < item.size; ++i, ++patched)
            put_u16(bytes, item.offset + i * 8, 9); // method_handle_item is 8 bytes, type first
    }
    ASSERT_GT(patched, 0u);

    auto ctx = load_dex_bytes(bytes);
    const AnnotationValue::MethodHandleValue empty{MethodHandleKind::InvokeStatic, MethodId{}};
    for (const char *name : {"getter", "invoker"}) {
        const auto v = handle_marker_element(ctx, name).value;
        ASSERT_TRUE(std::holds_alternative<AnnotationValue::MethodHandleValue>(v)) << name;
        EXPECT_EQ(std::get<AnnotationValue::MethodHandleValue>(v), empty) << name;
    }
}

TEST(ClassDefLookupTest, UnpatchedFileFindsEverything)
{
    auto d = annotated_dex();
    auto s = seen_in(d.bytes);
    EXPECT_TRUE(s.greet_marker && s.magic_marker && s.magic_value);
}

TEST(ClassDefLookupTest, DuplicateTypeUsesTheFirstClassDef)
{
    // Two class_defs name LAnnotated;: its own, and Marker's changed to name
    // it too.  The lookup takes the first one in class_defs order, whatever
    // the other one holds.
    auto d = annotated_dex();
    ASSERT_LT(d.annotated_def, d.marker_def) << "the fixture's class_def order changed";

    // Annotated's own class_def first: its annotations and static values are found.
    auto own_first = d;
    set_class_def_type(own_first, own_first.marker_def, own_first.annotated_type);
    auto s = seen_in(own_first.bytes);
    EXPECT_TRUE(s.greet_marker);
    EXPECT_TRUE(s.magic_marker);
    EXPECT_TRUE(s.magic_value);

    // Marker's class_def first: the lookup lands on it, and it has no member
    // annotations and no static values, even though Annotated's own class_def
    // comes right after it.
    auto other_first = d;
    swap_class_defs(other_first, d.annotated_def, d.marker_def);
    // Marker's class_def now sits at Annotated's old position, the earlier one.
    set_class_def_type(other_first, d.annotated_def, d.annotated_type);
    s = seen_in(other_first.bytes);
    EXPECT_FALSE(s.greet_marker);
    EXPECT_FALSE(s.magic_marker);
    EXPECT_FALSE(s.magic_value);
}

TEST(ClassDefLookupTest, TypeIndexPastTheTypeTable)
{
    // The members of Annotated name a type index past the end of the type
    // table.  No class_def names that index, so nothing is found.
    auto d = annotated_dex();
    const auto past = static_cast<uint16_t>(d.type_count + 3);

    auto unnamed = d;
    move_members_to_type(unnamed, static_cast<uint16_t>(unnamed.annotated_type), past);
    auto s = seen_in(unnamed.bytes);
    EXPECT_FALSE(s.greet_marker);
    EXPECT_FALSE(s.magic_marker);
    EXPECT_FALSE(s.magic_value);

    // Annotated's class_def names that same index too: it is found, even
    // though the index has no entry in the type table.
    auto named = d;
    move_members_to_type(named, static_cast<uint16_t>(named.annotated_type), past);
    set_class_def_type(named, named.annotated_def, past);
    s = seen_in(named.bytes);
    EXPECT_TRUE(s.greet_marker);
    EXPECT_TRUE(s.magic_marker);
    EXPECT_TRUE(s.magic_value);
}
