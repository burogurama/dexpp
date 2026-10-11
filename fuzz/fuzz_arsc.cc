// libFuzzer harness: the resources.arsc decoder (dex::apk::ResourceTable).
// On success it drives forward (name_of / resolve_string) and reverse (id_of)
// lookups across a swept id space so the package / type-chunk / entry-table
// walking in resources.cpp is exercised, including the sparse and OFFSET16
// entry layouts.  Seed from a resources.arsc extracted out of
// tests/data/sample.apk.

#include <cstddef>
#include <cstdint>
#include <span>

#include "apk/resources.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    auto table = dex::apk::ResourceTable::parse(std::span<const uint8_t>(data, size));
    if (!table)
        return 0;

    volatile std::size_t sink = 0;
    // Sweep a handful of resource ids across the typical package/type/entry
    // layout (0x7fTTEEEE) plus the framework package (0x01...).
    for (uint32_t pkg : {0x7fu, 0x01u}) {
        for (uint32_t type = 0; type < 4; ++type) {
            for (uint32_t entry = 0; entry < 8; ++entry) {
                uint32_t id = (pkg << 24) | (type << 16) | entry;
                if (auto name = table->name_of(id)) {
                    sink += name->type.size() + name->entry.size();
                    if (auto back = table->id_of(name->type, name->entry))
                        sink += *back;
                }
                if (auto s = table->resolve_string(id))
                    sink += s->size();
            }
        }
    }
    (void)sink;
    return 0;
}
