// read_whole_file() and the loaders built on it.  A path that is not a
// readable file must give an error: never an exception, never a huge
// allocation.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "dex.hpp"
#include "raw/file_io.hpp"
#include "raw/parser.hpp"

namespace {

const std::string kDirectory = "tests/data";
const std::string kMissing = "tests/data/no_such_file.bin";

/** A file in the temp directory, removed when the test ends. */
class TempFile
{
  public:
    TempFile(const std::string &name, const std::vector<uint8_t> &content)
        : path_((std::filesystem::temp_directory_path() / name).string())
    {
        std::ofstream out(path_, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(content.data()),
                  static_cast<std::streamsize>(content.size()));
    }
    ~TempFile() { std::filesystem::remove(path_); }
    const std::string &path() const { return path_; }

  private:
    std::string path_;
};

} // namespace

TEST(ReadWholeFile, ReadsEveryByte)
{
    // Larger than one read chunk, with every byte value in it.
    std::vector<uint8_t> content(200'000);
    for (std::size_t i = 0; i < content.size(); ++i)
        content[i] = static_cast<uint8_t>(i * 31);
    TempFile file("dexpp_file_io_test.bin", content);

    auto bytes = dex::raw::read_whole_file(file.path());
    ASSERT_TRUE(bytes.has_value()) << bytes.error().message;
    EXPECT_EQ(*bytes, content);
}

TEST(ReadWholeFile, ReadsAnEmptyFile)
{
    TempFile file("dexpp_file_io_empty.bin", {});
    auto bytes = dex::raw::read_whole_file(file.path());
    ASSERT_TRUE(bytes.has_value()) << bytes.error().message;
    EXPECT_TRUE(bytes->empty());
}

TEST(ReadWholeFile, MatchesTheFixture)
{
    auto bytes = dex::raw::read_whole_file("tests/data/classes.dex");
    ASSERT_TRUE(bytes.has_value()) << bytes.error().message;
    EXPECT_EQ(bytes->size(), std::filesystem::file_size("tests/data/classes.dex"));
}

TEST(ReadWholeFile, MissingPathIsNotFound)
{
    auto bytes = dex::raw::read_whole_file(kMissing);
    ASSERT_FALSE(bytes.has_value());
    EXPECT_EQ(bytes.error().kind, dex::raw::FileError::Kind::NotFound);
    EXPECT_NE(bytes.error().message.find(kMissing), std::string::npos);
}

TEST(ReadWholeFile, DirectoryIsCannotRead)
{
    auto bytes = dex::raw::read_whole_file(kDirectory);
    ASSERT_FALSE(bytes.has_value());
    EXPECT_EQ(bytes.error().kind, dex::raw::FileError::Kind::CannotRead);
    EXPECT_NE(bytes.error().message.find("directory"), std::string::npos);
}

// Each loader, given a directory.  Before read_whole_file() these asked for
// an impossible amount of memory (std::bad_alloc), and model::read_file let a
// std::ios_base::failure escape.

TEST(LoadersGivenADirectory, ParseFileReturnsAnError)
{
    auto dex = dex::raw::parser::parse_file(kDirectory);
    ASSERT_FALSE(dex.has_value());
    EXPECT_NE(dex.error().message.find("directory"), std::string::npos);
}

TEST(LoadersGivenADirectory, AnalysisContextReturnsCannotRead)
{
    auto from_dex = dex::AnalysisContext::from_dex(kDirectory);
    ASSERT_FALSE(from_dex.has_value());
    EXPECT_EQ(from_dex.error().code, dex::AnalysisError::Code::CannotRead);

    auto from_files = dex::AnalysisContext::from_dex_files({"tests/data/classes.dex", kDirectory});
    ASSERT_FALSE(from_files.has_value());
    EXPECT_EQ(from_files.error().code, dex::AnalysisError::Code::CannotRead);

    auto from_apk = dex::AnalysisContext::from_apk(kDirectory);
    ASSERT_FALSE(from_apk.has_value());
    EXPECT_EQ(from_apk.error().code, dex::AnalysisError::Code::CannotRead);
}

TEST(LoadersGivenADirectory, ApkOpenReturnsCannotRead)
{
    auto apk = dex::apk::Apk::open(kDirectory);
    ASSERT_FALSE(apk.has_value());
    EXPECT_EQ(apk.error().code, dex::AnalysisError::Code::CannotRead);
}

TEST(LoadersGivenADirectory, ModelReadFileReturnsIoWithoutThrowing)
{
    auto model = dex::model::read_file(kDirectory);
    ASSERT_FALSE(model.has_value());
    EXPECT_EQ(model.error().kind, dex::model::Error::Kind::Io);
}

TEST(LoadersGivenAMissingPath, StillReportFileNotFound)
{
    auto ctx = dex::AnalysisContext::from_dex(kMissing);
    ASSERT_FALSE(ctx.has_value());
    EXPECT_EQ(ctx.error().code, dex::AnalysisError::Code::FileNotFound);

    auto apk = dex::apk::Apk::open(kMissing);
    ASSERT_FALSE(apk.has_value());
    EXPECT_EQ(apk.error().code, dex::AnalysisError::Code::FileNotFound);

    auto model = dex::model::read_file(kMissing);
    ASSERT_FALSE(model.has_value());
    EXPECT_EQ(model.error().kind, dex::model::Error::Kind::Io);
}
