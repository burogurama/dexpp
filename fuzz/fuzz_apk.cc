// libFuzzer harness: the full package path.  from_apk_buffer drives the ZIP
// reader (raw/zip.cpp), multidex ordering, and DEFLATE inflation; on success
// the harness walks the lazy analysis surfaces — class data, instructions,
// CFGs, and the three whole-program indices — which is where the analysis
// layer's "malformed pool index -> empty result" access checks live (they are
// reached at access time, so a parse-only fuzzer never touches them).  Seed
// this from tests/data/*.apk.

#include <cstddef>
#include <cstdint>
#include <span>

#include "dex.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    auto ctx = dex::AnalysisContext::from_apk_buffer(std::span<const uint8_t>(data, size));
    if (!ctx)
        return 0;

    volatile std::size_t sink = 0;
    for (const auto &cls : ctx->classes()) {
        sink += cls.name().size();
        for (const auto &m : cls.methods()) {
            sink += m.instructions().size();
            sink += m.cfg().blocks().size();
            sink += m.calls().size();
            sink += m.field_accesses().size();
            sink += m.string_loads().size();
            sink += m.try_blocks().size();
        }
        for (const auto &f : cls.fields())
            sink += f.name().size();
    }

    // Whole-program singletons: each walks every loaded method once.
    sink += ctx->call_graph().nodes().size();
    sink += ctx->class_hierarchy().empty() ? 0 : 1;
    sink += ctx->xrefs().referenced_strings().size();
    sink += ctx->strings().size();
    (void)sink;
    return 0;
}
