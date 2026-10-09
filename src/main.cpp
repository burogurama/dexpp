#include <cstdio>
#include <iostream>

#include "dex.hpp"

static void print_instruction(const dex::Instruction &insn)
{
    std::printf("    [%04x] %s\n", dex::base_of(insn).offset, dex::to_string(insn).c_str());
}

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
        std::cout << "Class: " << cls.name() << '\n';

        for (const auto &method : cls.methods()) {
            std::printf("  %s\n", method.name().data());

            auto insns = method.instructions();
            if (insns.empty()) {
                std::puts("    (abstract / native)");
                continue;
            }

            for (const auto &insn : insns)
                print_instruction(insn);
        }

        std::putchar('\n');
    }
}
