#include <format>
#include <iostream>

#include "dex.hpp"

int main(int argc, const char *argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: main_test <file.dex>\n";
        return 1;
    }

    auto ctx = dex::AnalysisContext::from_dex(argv[1]);
    if (!ctx) {
        std::cerr << "Error: " << ctx.error().message << '\n';
        return 1;
    }

    for (const auto &cls : ctx->classes()) {
        std::cout << "Class: " << cls.descriptor() << '\n';

        for (const auto &method : cls.methods()) {
            std::cout << "  " << method.name() << method.proto() << '\n';

            if (!method.has_code()) {
                std::cout << "    (abstract / native)\n";
                continue;
            }

            // Each instruction with its references named; dex::to_string()
            // prints the same instructions with raw pool indices.
            for (const auto &line : method.disassemble())
                std::cout << std::format("    [{:04x}] {}\n", line.offset, line.text);
        }

        std::cout << '\n';
    }
}
