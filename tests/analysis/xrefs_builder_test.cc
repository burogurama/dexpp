// The cross-reference index is built by scanning the code without decoding it
// (detail::find_refs) and resolving names straight from the DEX files, on one
// thread per DEX file.  These tests compare it with a straightforward
// reference built from the per-method API — the decoded instructions and the
// forward references — on every fixture, and check that the thread count
// changes nothing.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../raw/test_utils.hpp"
#include "dex.hpp"

using namespace dex;

namespace {

std::string text(const MethodId &id) { return id.class_descriptor + "->" + id.name + id.proto; }
std::string text(const FieldId &id) { return id.class_descriptor + "->" + id.name + ":" + id.type; }
std::string site(uint32_t referrer, uint32_t offset)
{
    return " " + std::to_string(referrer) + "@" + std::to_string(offset);
}

// What the index must hold, built the plain way: every method of every class
// definition that wins (the one find_class() returns), in classes() order,
// direct then virtual; a method is a referrer when its forward references are
// not empty, numbered in that order.
struct Reference
{
    std::vector<Method> referrers;
    std::map<std::string, std::string> strings; // value -> its sites, as text
    std::map<std::string, std::string> types;
    std::map<std::string, std::pair<FieldId, std::string>> fields;
    std::map<std::string, std::pair<MethodId, std::string>> methods;
};

Reference build_reference(const AnalysisContext &ctx)
{
    Reference ref;
    for (const Class &cls : ctx.classes()) {
        if (ctx.find_class(cls.descriptor()) != cls)
            continue;
        for (const Method &m : cls.methods()) {
            std::vector<StringLoad> strings = m.string_loads();
            std::vector<TypeUse> types = m.type_uses();
            std::vector<FieldAccess> fields = m.field_accesses();
            std::vector<MethodCall> calls = m.calls();
            if (strings.empty() && types.empty() && fields.empty() && calls.empty())
                continue;
            const auto r = static_cast<uint32_t>(ref.referrers.size());
            ref.referrers.push_back(m);
            for (const StringLoad &s : strings)
                ref.strings[std::string(s.value)] += site(r, s.code_offset);
            for (const TypeUse &t : types)
                ref.types[std::string(t.descriptor)] +=
                    site(r, t.code_offset) + "/" + std::to_string(static_cast<int>(t.kind));
            for (const FieldAccess &f : fields) {
                auto &entry = ref.fields[text(f.field)];
                entry.first = f.field;
                entry.second += site(r, f.code_offset) + "/" + (f.is_write ? "w" : "r") +
                                (f.is_static ? "s" : "i");
            }
            for (const MethodCall &c : calls) {
                auto &entry = ref.methods[text(c.target)];
                entry.first = c.target;
                entry.second +=
                    site(r, c.code_offset) + "/" + std::to_string(static_cast<int>(c.kind));
            }
        }
    }
    return ref;
}

// Compare the index with the reference, key by key, in the same text.
void expect_matches_reference(const AnalysisContext &ctx, const std::string &where)
{
    const Reference ref = build_reference(ctx);
    const Xrefs &x = ctx.xrefs();

    ASSERT_EQ(x.referrers().size(), ref.referrers.size()) << where;
    for (uint32_t r = 0; r < ref.referrers.size(); ++r) {
        EXPECT_EQ(x.referrer_id(r), ref.referrers[r].id()) << where << " referrer " << r;
        std::optional<Method> m = x.referrer_method(r);
        ASSERT_TRUE(m.has_value()) << where << " referrer " << r;
        EXPECT_EQ(*m, ref.referrers[r]) << where << " referrer " << r;
    }

    // Strings can be listed, so the index must not hold one more or one less.
    std::vector<std::string_view> listed = x.referenced_strings();
    std::set<std::string> listed_set(listed.begin(), listed.end());
    EXPECT_EQ(listed_set.size(), listed.size()) << where << ": a string is listed twice";
    std::set<std::string> expected_strings;
    for (const auto &[value, sites] : ref.strings)
        expected_strings.insert(value);
    EXPECT_EQ(listed_set, expected_strings) << where;

    for (const auto &[value, expected] : ref.strings) {
        std::string actual;
        for (const StringXref &s : x.string_refs(value))
            actual += site(s.referrer, s.code_offset);
        EXPECT_EQ(actual, expected) << where << " string \"" << value << '"';
    }
    for (const auto &[descriptor, expected] : ref.types) {
        std::string actual;
        for (const TypeXref &t : x.type_refs(descriptor))
            actual +=
                site(t.referrer, t.code_offset) + "/" + std::to_string(static_cast<int>(t.kind));
        EXPECT_EQ(actual, expected) << where << " type " << descriptor;
    }
    for (const auto &[key, entry] : ref.fields) {
        std::string actual;
        std::size_t reads = 0;
        for (const FieldXref &f : x.field_refs(entry.first)) {
            actual += site(f.referrer, f.code_offset) + "/" + (f.is_write ? "w" : "r") +
                      (f.is_static ? "s" : "i");
            reads += f.is_write ? 0 : 1;
        }
        EXPECT_EQ(actual, entry.second) << where << " field " << key;
        EXPECT_EQ(x.field_reads(entry.first).size(), reads) << where << " field " << key;
        EXPECT_EQ(x.field_writes(entry.first).size(), x.field_refs(entry.first).size() - reads)
            << where << " field " << key;
    }

    // Methods, by identity and by bare name.
    std::map<std::string, std::set<std::string>> by_name;
    for (const auto &[key, entry] : ref.methods) {
        std::string actual;
        for (const MethodXref &c : x.method_refs(entry.first))
            actual +=
                site(c.referrer, c.code_offset) + "/" + std::to_string(static_cast<int>(c.kind));
        EXPECT_EQ(actual, entry.second) << where << " method " << key;
        by_name[entry.first.name].insert(key + entry.second);
    }
    for (const auto &[name, expected] : by_name) {
        std::set<std::string> actual;
        for (const auto &[id, sites] : x.method_refs_by_name(name)) {
            std::string line = text(*id);
            for (const MethodXref &c : sites)
                line += site(c.referrer, c.code_offset) + "/" +
                        std::to_string(static_cast<int>(c.kind));
            actual.insert(line);
        }
        EXPECT_EQ(actual, expected) << where << " name " << name;
    }
}

// Everything the index can answer about the keys it lists, as text, in the
// order the index gives it (so iteration order is compared too).
std::vector<std::string> describe(const Xrefs &x)
{
    std::vector<std::string> out;
    for (const MethodId &id : x.referrers())
        out.push_back("R " + text(id));
    std::set<std::string> names;
    for (std::string_view value : x.referenced_strings()) {
        std::string line = "S " + std::string(value);
        for (const StringXref &s : x.string_refs(value))
            line += site(s.referrer, s.code_offset);
        out.push_back(line);
    }
    for (uint32_t r = 0; r < x.referrers().size(); ++r) {
        std::optional<Method> m = x.referrer_method(r);
        for (const MethodCall &c : m->calls())
            names.insert(c.target.name);
        for (const TypeUse &t : m->type_uses()) {
            std::string line = "T " + std::string(t.descriptor);
            for (const TypeXref &u : x.type_refs(t.descriptor))
                line += site(u.referrer, u.code_offset);
            out.push_back(line);
        }
        for (const FieldAccess &f : m->field_accesses()) {
            std::string line = "F " + text(f.field);
            for (const FieldXref &a : x.field_refs(f.field))
                line += site(a.referrer, a.code_offset);
            out.push_back(line);
        }
    }
    for (const std::string &name : names)
        for (const auto &[id, sites] : x.method_refs_by_name(name)) {
            std::string line = "M " + text(*id);
            for (const MethodXref &c : sites)
                line += site(c.referrer, c.code_offset);
            out.push_back(line);
        }
    return out;
}

} // namespace

TEST(XrefsBuilderTest, MatchesReferenceOnEveryFixture)
{
    std::size_t checked = 0;
    for (const char *dir : {"tests/data", "tests/data/local"}) {
        if (!std::filesystem::is_directory(dir))
            continue;
        std::vector<std::string> paths;
        for (const auto &entry : std::filesystem::directory_iterator(dir))
            if (entry.path().extension() == ".dex" || entry.path().extension() == ".apk")
                paths.push_back(entry.path().generic_string());
        std::sort(paths.begin(), paths.end());
        for (const std::string &path : paths) {
            auto ctx = path.ends_with(".dex") ? AnalysisContext::from_dex(path)
                                              : AnalysisContext::from_apk(path);
            if (!ctx.has_value())
                continue; // an APK without code, such as nodex.apk
            expect_matches_reference(*ctx, path);
            ++checked;
        }
    }
    EXPECT_GE(checked, 19u) << "every tracked DEX and APK fixture with code";

    // Several DEX files in one context: cross-DEX references, and a class
    // defined twice (only its first definition's code is indexed).
    const std::vector<std::vector<std::string>> sets = {
        {"tests/data/inherit_a.dex", "tests/data/inherit_b.dex"},
        {"tests/data/classes.dex", "tests/data/fibonacci.dex", "tests/data/shapes.dex"},
        {"tests/data/classes.dex", "tests/data/classes.dex"},
    };
    for (const auto &files : sets) {
        auto ctx = AnalysisContext::from_dex_files(files);
        ASSERT_TRUE(ctx.has_value()) << files[0];
        expect_matches_reference(*ctx, files[0] + " and others");
    }
}

TEST(XrefsBuilderTest, SameIndexWithAnyThreadCount)
{
    // The DEX files are scanned on several threads and the maps are built on
    // several threads; nothing about the result may depend on that.  An
    // explicit count is used even for these small fixtures, so this runs the
    // threaded path (and the thread sanitizer job checks it).
    for (const char *path :
         {"tests/data/multidex.apk", "tests/data/app.apk", "tests/data/local/multidex.apk"}) {
        if (missing_local_fixture(path))
            continue;
        auto one = AnalysisContext::from_apk(path, ApkLoadOptions{.threads = 1});
        auto four = AnalysisContext::from_apk(path, ApkLoadOptions{.threads = 4});
        auto automatic = AnalysisContext::from_apk(path);
        ASSERT_TRUE(one.has_value() && four.has_value() && automatic.has_value()) << path;
        std::vector<std::string> expected = describe(one->xrefs());
        EXPECT_FALSE(one->xrefs().referrers().empty()) << path;
        EXPECT_EQ(describe(four->xrefs()), expected) << path;
        EXPECT_EQ(describe(automatic->xrefs()), expected) << path;
        expect_matches_reference(*four, path);
    }
}
