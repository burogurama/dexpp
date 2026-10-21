// Every fixture DEX (and every DEX inside the fixture APKs) is read into the
// model and written back.  The output must pass the format checks, write
// back to the same bytes (a fixpoint) and read back to the same model.  And —
// seen through the analysis layer, which shares no code with the model — the
// files up to 1 MB must hold the same classes, members, flags, annotations,
// initial values, calls, field accesses, strings, type uses and exception
// handlers before and after.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

#include "../raw/test_utils.hpp"
#include "describe.hpp"
#include "dex.hpp"
#include "dex_check.hpp"
#include "model/model.hpp"
#include "raw/zip.hpp"

namespace {

struct Input
{
    std::string name;
    std::vector<uint8_t> bytes;
};

std::vector<Input> fixtures()
{
    std::vector<Input> out;
    for (const char *dir : {"tests/data", "tests/data/androguard"})
        for (const auto &entry : std::filesystem::directory_iterator(dir)) {
            const auto &p = entry.path();
            if (p.filename().string().starts_with("bench")) // generated locally, not a fixture
                continue;
            if (p.extension() == ".dex")
                out.push_back({p.string(), load_buf(p.string())});
            if (p.extension() != ".apk")
                continue;
            auto apk = load_buf(p.string());
            auto names = dex::raw::zip::entry_names(apk);
            if (!names)
                continue;
            static const std::regex dex_entry("classes[0-9]*\\.dex");
            for (const std::string &name : *names)
                if (std::regex_match(name, dex_entry))
                    if (auto bytes = dex::raw::zip::read_entry(apk, name))
                        out.push_back({p.string() + "!" + name, std::move(*bytes)});
        }
    std::sort(out.begin(), out.end(),
              [](const Input &a, const Input &b) { return a.name < b.name; });
    return out;
}

void round_trip(const Input &in, std::size_t index)
{
    SCOPED_TRACE(in.name);

    auto model = dex::model::read(in.bytes);
    ASSERT_TRUE(model.has_value()) << model.error().message;
    auto out = dex::model::write(*model);
    ASSERT_TRUE(out.has_value()) << out.error().message;

    for (const std::string &p : check_dex(*out))
        ADD_FAILURE() << p;

    auto again = dex::model::read(*out);
    ASSERT_TRUE(again.has_value()) << again.error().message;
    EXPECT_TRUE(*again == *model) << "re-reading the output gives a different model";
    auto out2 = dex::model::write(*again);
    ASSERT_TRUE(out2.has_value());
    EXPECT_TRUE(*out2 == *out) << "writing is not a fixpoint";

    // The analysis-layer comparison is the slow part; the large fixtures get
    // every other check (the local corpus runs cover large apps fully).
    if (in.bytes.size() > (1 << 20))
        return;
    std::string tag = std::to_string(index);
    std::string original = temp_file(tag + "_in", in.bytes);
    std::string written = temp_file(tag + "_out", *out);
    auto before = describe_file(original);
    auto after = describe_file(written);
    std::filesystem::remove(original);
    std::filesystem::remove(written);
    EXPECT_EQ(before.size(), after.size());
    auto mismatch = std::mismatch(before.begin(), before.end(), after.begin(), after.end());
    if (mismatch.first != before.end() || mismatch.second != after.end())
        ADD_FAILURE() << "first difference:\n  before: "
                      << (mismatch.first != before.end() ? *mismatch.first : "<end>")
                      << "\n  after:  "
                      << (mismatch.second != after.end() ? *mismatch.second : "<end>");
}

} // namespace

TEST(ModelRoundTrip, EveryFixtureWritesBackTheSameProgram)
{
    std::vector<Input> inputs = fixtures();
    EXPECT_GE(inputs.size(), 25u);
    for (std::size_t i = 0; i < inputs.size(); ++i)
        round_trip(inputs[i], i);
}
