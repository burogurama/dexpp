// The first client: splitting one DEX file into one file per class.  Each
// piece is just a model with one class; loaded together, the pieces hold the
// same program as the original.

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "../raw/test_utils.hpp"
#include "describe.hpp"
#include "dex_check.hpp"
#include "model/model.hpp"

TEST(ModelSplit, OneClassPerFileHoldsTheSameProgram)
{
    for (const char *input : {"tests/data/inherit.dex", "tests/data/androguard/classes.dex"}) {
        SCOPED_TRACE(input);
        auto dex = dex::model::read_file(input);
        ASSERT_TRUE(dex.has_value());
        ASSERT_GT(dex->classes.size(), 1u);

        std::vector<std::string> paths;
        for (std::size_t i = 0; i < dex->classes.size(); ++i) {
            dex::model::Dex piece{dex->version, {dex->classes[i]}};
            auto bytes = dex::model::write(piece);
            ASSERT_TRUE(bytes.has_value()) << bytes.error().message;
            for (const std::string &p : check_dex(*bytes))
                ADD_FAILURE() << dex->classes[i].type << ": " << p;
            paths.push_back(temp_file("split" + std::to_string(i), *bytes));
        }

        auto whole = describe_file(input);
        auto pieces = dex::AnalysisContext::from_dex_files(paths);
        for (const std::string &p : paths)
            std::filesystem::remove(p);
        ASSERT_TRUE(pieces.has_value());
        EXPECT_EQ(describe(*pieces), whole);
    }
}
