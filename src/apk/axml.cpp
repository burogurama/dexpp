#include "axml.hpp"

#include <algorithm>
#include <utility>

#include "apk/string_pool.hpp"

namespace dex::axml {

namespace {

using dex::apk::detail::parse_string_pool;
using dex::apk::detail::rd16;
using dex::apk::detail::rd32;
using dex::apk::detail::StringPool;

// Chunk types (ResourceTypes.h).
constexpr uint16_t RES_STRING_POOL_TYPE = 0x0001;
constexpr uint16_t RES_XML_TYPE = 0x0003;
constexpr uint16_t RES_XML_START_NAMESPACE_TYPE = 0x0100;
constexpr uint16_t RES_XML_END_NAMESPACE_TYPE = 0x0101;
constexpr uint16_t RES_XML_START_ELEMENT_TYPE = 0x0102;
constexpr uint16_t RES_XML_END_ELEMENT_TYPE = 0x0103;
constexpr uint16_t RES_XML_CDATA_TYPE = 0x0104;
constexpr uint16_t RES_XML_RESOURCE_MAP_TYPE = 0x0180;

constexpr uint32_t NO_ENTRY = 0xFFFFFFFF;

} // namespace

std::expected<XmlDocument, AxmlError> parse(std::span<const uint8_t> buf)
{
    if (buf.size() < 8)
        return std::unexpected(AxmlError{"buffer too small to be binary XML"});
    if (rd16(buf, 0) != RES_XML_TYPE)
        return std::unexpected(AxmlError{"not a binary XML document (bad chunk type)"});

    std::size_t end = std::min<std::size_t>(buf.size(), rd32(buf, 4));
    std::size_t pos = rd16(buf, 2); // file header size

    StringPool pool;
    XmlNode virtual_root;
    std::vector<XmlNode *> stack{&virtual_root};

    while (pos + 8 <= end) {
        uint16_t ctype = rd16(buf, pos);
        uint16_t header_size = rd16(buf, pos + 2);
        uint32_t chunk_size = rd32(buf, pos + 4);
        if (chunk_size < 8 || pos + chunk_size > end)
            return std::unexpected(AxmlError{"malformed chunk size"});
        std::size_t next = pos + chunk_size;
        std::size_t ext = pos + header_size; // element payload after node header

        switch (ctype) {
        case RES_STRING_POOL_TYPE:
            pool = parse_string_pool(buf, pos, next);
            break;

        case RES_XML_START_ELEMENT_TYPE: {
            if (ext + 16 > end)
                return std::unexpected(AxmlError{"truncated start-element chunk"});
            XmlNode node;
            node.name = std::string(pool.get(rd32(buf, ext + 4)));

            uint16_t attr_start = rd16(buf, ext + 8);
            uint16_t attr_size = rd16(buf, ext + 10);
            uint16_t attr_count = rd16(buf, ext + 12);
            std::size_t a = ext + attr_start;
            for (uint16_t i = 0; i < attr_count; ++i, a += attr_size) {
                if (a + 20 > end)
                    break;
                XmlAttribute attr;
                attr.namespace_uri = std::string(pool.get(rd32(buf, a)));
                attr.name = std::string(pool.get(rd32(buf, a + 4)));
                attr.typed.type = buf[a + 15];
                attr.typed.data = rd32(buf, a + 16);
                uint32_t raw_idx = rd32(buf, a + 8);
                if (raw_idx != NO_ENTRY)
                    attr.raw_value = std::string(pool.get(raw_idx));
                else if (attr.typed.is(TypedValue::Type::String))
                    attr.raw_value = std::string(pool.get(attr.typed.data));
                node.attributes.push_back(std::move(attr));
            }

            stack.back()->children.push_back(std::move(node));
            stack.push_back(&stack.back()->children.back());
            break;
        }

        case RES_XML_END_ELEMENT_TYPE:
            if (stack.size() > 1)
                stack.pop_back();
            break;

        case RES_XML_CDATA_TYPE:
            if (ext + 4 <= end)
                stack.back()->text += pool.get(rd32(buf, ext));
            break;

        case RES_XML_START_NAMESPACE_TYPE:
        case RES_XML_END_NAMESPACE_TYPE:
        case RES_XML_RESOURCE_MAP_TYPE:
        default:
            break; // structurally irrelevant for the tree
        }

        pos = next;
    }

    if (virtual_root.children.empty())
        return std::unexpected(AxmlError{"document has no root element"});

    XmlDocument doc;
    doc.root = std::move(virtual_root.children.front());
    return doc;
}

} // namespace dex::axml
