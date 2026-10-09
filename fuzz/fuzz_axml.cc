// libFuzzer harness: the binary-XML (AXML) decoder (dex::axml::parse), one of
// the newer, hand-rolled binary parsers.  On success it recurses the whole
// element tree and reads typed attribute values, so the string-pool and
// resource-map indexing in axml.cpp / string_pool.cpp is exercised.  Seed from
// an AndroidManifest.xml extracted out of tests/data/sample.apk.

#include <cstddef>
#include <cstdint>
#include <span>

#include "apk/axml.hpp"

namespace {

std::size_t walk(const dex::axml::XmlNode &node)
{
    std::size_t sink = node.name.size() + node.text.size();
    for (const auto &a : node.attributes) {
        sink += a.name.size();
        if (auto s = a.as_string())
            sink += s->size();
        if (auto i = a.as_int())
            sink += static_cast<std::size_t>(*i);
        if (auto r = a.as_reference())
            sink += *r;
        sink += a.as_bool().value_or(false) ? 1 : 0;
    }
    for (const auto &c : node.children)
        sink += walk(c);
    return sink;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (auto doc = dex::axml::parse(std::span<const uint8_t>(data, size))) {
        volatile std::size_t sink = walk(doc->root);
        (void)sink;
    }
    return 0;
}
