// Edit a DEX file with the model: change a string constant inside a method,
// then split the file into one DEX file per class.
//
//   example_edit [input.dex [output-directory]]

#include <cstddef>
#include <expected>
#include <filesystem>
#include <iostream>
#include <string>
#include <variant>

#include "dex.hpp"

int main(int argc, char **argv)
{
    const std::string input_path = argc > 1 ? argv[1] : "tests/data/classes.dex";
    const std::filesystem::path out_dir =
        argc > 2 ? argv[2] : std::filesystem::temp_directory_path() / "dexpp_example_edit";
    std::filesystem::create_directories(out_dir);

    // --8<-- [start:read]
    std::expected<dex::model::Dex, dex::Error> read = dex::model::read_file(input_path);
    if (!read) {
        std::cerr << read.error().message << '\n';
        return 1;
    }
    dex::model::Dex &input = *read;
    // --8<-- [end:read]

    // --8<-- [start:edit]
    // Code is a list of items.  An item is an instruction, a label, or a debug
    // marker, and an instruction is one of the model's instruction structs.
    std::size_t changed = 0;
    for (dex::model::ClassDef &cls : input.classes)
        for (dex::model::MethodDef &method : cls.methods) {
            if (!method.code)
                continue; // abstract or native
            for (dex::model::Item &item : method.code->items) {
                dex::model::Insn *insn = std::get_if<dex::model::Insn>(&item);
                if (insn == nullptr)
                    continue;
                dex::model::ConstString *load = std::get_if<dex::model::ConstString>(insn);
                if (load != nullptr) {
                    load->value = "[" + *load->value + "]";
                    ++changed;
                }
            }
        }
    std::cout << "changed " << changed << " string constants\n";
    // --8<-- [end:edit]

    // --8<-- [start:validate]
    for (const dex::Problem &problem : dex::model::validate(input))
        std::cerr << problem.where << ": " << problem.message << '\n';
    // --8<-- [end:validate]

    // --8<-- [start:split]
    // A class holds its references as names, not as indices into the file's
    // tables, so it can go into another file as it is.
    for (std::size_t i = 0; i < input.classes.size(); ++i) {
        dex::model::Dex one{input.version, {input.classes[i]}};
        const std::string path = (out_dir / ("class" + std::to_string(i) + ".dex")).string();
        std::expected<void, dex::Error> written = dex::model::write_file(one, path);
        if (!written) {
            std::cerr << written.error().message << '\n';
            return 1;
        }
        std::cout << "wrote " << input.classes[i].type << '\n';
    }
    // --8<-- [end:split]
    return 0;
}
