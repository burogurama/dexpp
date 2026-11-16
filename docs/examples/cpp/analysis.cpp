// Open an APK, read its manifest, list its classes and bytecode, and ask the
// call graph what a method may call.  Then open the same APK from bytes in
// memory.
//
//   example_analysis [file.apk]

#include <cstdint>
#include <expected>
#include <fstream>
#include <iostream>
#include <utility>
#include <vector>

#include "dex.hpp"

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "tests/data/sample.apk";

    // --8<-- [start:open]
    std::expected<dex::apk::Apk, dex::Error> apk = dex::apk::Apk::open(path);
    if (!apk) {
        std::cerr << apk.error().message << '\n';
        return 1;
    }

    // manifest() is null when the APK has no manifest it can decode.
    if (const dex::apk::Manifest *manifest = apk->manifest()) {
        std::cout << "package: " << manifest->package() << '\n';
        for (const dex::apk::Component &component : manifest->components())
            if (component.is_reachable())
                std::cout << "reachable: " << component.name << '\n';
    }
    // --8<-- [end:open]

    // --8<-- [start:code]
    std::expected<dex::AnalysisContext, dex::Error> ctx = apk->analysis();
    if (!ctx) {
        std::cerr << ctx.error().message << '\n';
        return 1;
    }

    for (const dex::Class &cls : ctx->classes()) {
        std::cout << cls.descriptor() << '\n';
        for (const dex::Method &method : cls.methods()) {
            std::cout << "  " << method.name() << '\n';
            for (const dex::Instruction &insn : method.instructions()) {
                const uint32_t offset = dex::base_of(insn).offset;
                std::cout << "    " << offset << ": " << dex::to_string(insn) << '\n';
            }
        }
    }
    // --8<-- [end:code]

    // --8<-- [start:graph]
    const dex::CallGraph &graph = ctx->call_graph();
    const dex::MethodId main_id{"LTestDex;", "main", "([Ljava/lang/String;)V"};
    if (const dex::CallNode *node = graph.find(main_id)) {
        for (uint32_t index : graph.callees(node->index)) {
            const dex::MethodId &callee = graph.nodes()[index].id;
            std::cout << "main may call " << callee.class_descriptor << "->" << callee.name << '\n';
        }
    }
    // --8<-- [end:graph]

    // --8<-- [start:memory]
    // The bytes can come from anywhere: a download, an upload, a database.
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> bytes;
    for (char c; file.get(c);)
        bytes.push_back(static_cast<uint8_t>(c));

    // Moving the vector in gives its memory to the Apk: nothing is copied.
    std::expected<dex::apk::Apk, dex::Error> in_memory =
        dex::apk::Apk::from_buffer(std::move(bytes), "upload.apk");
    if (!in_memory) {
        std::cerr << in_memory.error().message << '\n';
        return 1;
    }

    // {.threads = 1} keeps all the work, the call graph included, on this thread.
    std::expected<dex::AnalysisContext, dex::Error> code = in_memory->analysis({.threads = 1});
    if (!code) {
        std::cerr << code.error().message << '\n';
        return 1;
    }
    for (const dex::Class &cls : code->classes())
        std::cout << "in upload.apk: " << cls.descriptor() << '\n';
    // --8<-- [end:memory]
    return 0;
}
