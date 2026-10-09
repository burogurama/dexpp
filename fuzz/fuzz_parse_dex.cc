// libFuzzer harness: the raw DEX parser (dex::raw::parser::parse_buffer), the
// core untrusted-input path.  Parses each input twice — eager and lazy string
// decode, the two modes analysis and the raw layer use — and, on success,
// touches every id-table accessor so the overflow-safe bounds arithmetic in
// raw/parser.cpp is exercised under ASan/UBSan, not just the happy path.

#include <cstddef>
#include <cstdint>
#include <span>

#include "raw/parser.hpp"
#include "raw/types.hpp"

namespace {

void walk(const dex::raw::DexFile &dex)
{
    // Force the accessors that hand out spans into internal vectors; iterate
    // enough of each to catch a size/offset the parser accepted but shouldn't
    // have.
    volatile std::size_t sink = 0;
    sink += dex.string_ids().size();
    sink += dex.type_ids().size();
    sink += dex.proto_ids().size();
    sink += dex.field_ids().size();
    sink += dex.method_ids().size();
    sink += dex.class_defs().size();
    sink += dex.strings().size();
    for (const auto &c : dex.class_defs())
        sink += c.class_idx;
    for (const auto &m : dex.method_ids())
        sink += m.name_idx;
    (void)sink;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    std::span<const uint8_t> buf(data, size);

    if (auto eager = dex::raw::parser::parse_buffer(buf, {.decode_strings = true}))
        walk(*eager);
    if (auto lazy = dex::raw::parser::parse_buffer(buf, {.decode_strings = false}))
        walk(*lazy);

    return 0;
}
