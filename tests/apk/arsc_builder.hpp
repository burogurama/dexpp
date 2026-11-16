#pragma once

// A small resources.arsc writer for tests that need a table aapt2 would not
// write: typeSpec flags that do not match the values, reference loops in one
// configuration, configurations aapt2 has no folder name for, and so on.
//
// The table has one package, id 0x7f, named "com.test", with one type, id 1,
// named "res".  Entry i is resource 0x7f010000 + i and is named "e<i>".

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace dex::test {

/** A ResTable_config: 64 bytes, all "any" (0) until a setter changes them. */
struct ArscConfig
{
    std::array<uint8_t, 64> bytes{};

    ArscConfig &language(char a, char b)
    {
        bytes[8] = static_cast<uint8_t>(a);
        bytes[9] = static_cast<uint8_t>(b);
        return *this;
    }
    ArscConfig &region(char a, char b)
    {
        bytes[10] = static_cast<uint8_t>(a);
        bytes[11] = static_cast<uint8_t>(b);
        return *this;
    }
    ArscConfig &raw(std::size_t offset, uint8_t value)
    {
        bytes[offset] = value;
        return *this;
    }
    ArscConfig &sdk(uint16_t version)
    {
        bytes[24] = static_cast<uint8_t>(version);
        bytes[25] = static_cast<uint8_t>(version >> 8);
        return *this;
    }
    bool operator<(const ArscConfig &o) const { return bytes < o.bytes; }
};

class ArscBuilder
{
  public:
    static constexpr uint8_t kReference = 0x01;
    static constexpr uint8_t kString = 0x03;
    static constexpr uint8_t kIntDec = 0x10;
    static constexpr uint8_t kBoolean = 0x12;

    static constexpr uint32_t id(uint16_t entry) { return 0x7f010000u | entry; }

    /** Add @p text to the global string pool; returns its index. */
    uint32_t string(const std::string &text)
    {
        strings_.push_back(text);
        return static_cast<uint32_t>(strings_.size() - 1);
    }

    /** Give entry @p entry the value (@p type, @p data) in @p config. */
    ArscBuilder &value(uint16_t entry, const ArscConfig &config, uint8_t type, uint32_t data)
    {
        configs_[config].push_back({entry, type, data});
        if (entry + 1u > entry_count_)
            entry_count_ = entry + 1u;
        return *this;
    }

    /** Set the typeSpec flags of entry @p entry. */
    ArscBuilder &spec_flags(uint16_t entry, uint32_t flags)
    {
        spec_flags_[entry] = flags;
        if (entry + 1u > entry_count_)
            entry_count_ = entry + 1u;
        return *this;
    }

    std::vector<uint8_t> build() const
    {
        std::vector<uint8_t> package;
        std::vector<std::string> keys;
        for (uint32_t i = 0; i < entry_count_; ++i)
            keys.push_back("e" + std::to_string(i));
        const std::vector<uint8_t> type_pool = pool({"res"});
        const std::vector<uint8_t> key_pool = pool(keys);

        constexpr uint32_t kPackageHeader = 288;
        put16(package, 0x0200);
        put16(package, kPackageHeader);
        put32(package, 0); // size, patched below
        put32(package, 0x7f);
        const std::string name = "com.test";
        for (std::size_t i = 0; i < 128; ++i)
            put16(package, i < name.size() ? static_cast<uint8_t>(name[i]) : 0);
        put32(package, kPackageHeader); // typeStrings
        put32(package, 0);
        put32(package, kPackageHeader + static_cast<uint32_t>(type_pool.size())); // keyStrings
        put32(package, 0);
        put32(package, 0);
        package.insert(package.end(), type_pool.begin(), type_pool.end());
        package.insert(package.end(), key_pool.begin(), key_pool.end());

        // typeSpec
        put16(package, 0x0202);
        put16(package, 16);
        put32(package, 16 + 4 * entry_count_);
        package.push_back(1); // type id
        package.push_back(0);
        put16(package, 0);
        put32(package, entry_count_);
        for (uint32_t i = 0; i < entry_count_; ++i) {
            auto it = spec_flags_.find(static_cast<uint16_t>(i));
            put32(package, it == spec_flags_.end() ? 0 : it->second);
        }

        // one type chunk per configuration
        for (const auto &[config, values] : configs_) {
            constexpr uint32_t kHeader = 20 + 64;
            const uint32_t entries_start = kHeader + 4 * entry_count_;
            std::vector<uint32_t> offsets(entry_count_, 0xFFFFFFFF);
            std::vector<uint8_t> data;
            for (const Value &v : values) {
                offsets[v.entry] = static_cast<uint32_t>(data.size());
                put16(data, 8); // ResTable_entry size
                put16(data, 0); // flags
                put32(data, v.entry);
                put16(data, 8); // Res_value size
                data.push_back(0);
                data.push_back(v.type);
                put32(data, v.data);
            }
            put16(package, 0x0201);
            put16(package, kHeader);
            put32(package, entries_start + static_cast<uint32_t>(data.size()));
            package.push_back(1); // type id
            package.push_back(0); // flags
            put16(package, 0);
            put32(package, entry_count_);
            put32(package, entries_start);
            std::array<uint8_t, 64> bytes = config.bytes;
            bytes[0] = 64; // size
            package.insert(package.end(), bytes.begin(), bytes.end());
            for (uint32_t offset : offsets)
                put32(package, offset);
            package.insert(package.end(), data.begin(), data.end());
        }
        const uint32_t package_size = static_cast<uint32_t>(package.size());
        for (int i = 0; i < 4; ++i)
            package[4 + i] = static_cast<uint8_t>(package_size >> (8 * i));

        std::vector<uint8_t> out;
        const std::vector<uint8_t> values_pool = pool(strings_);
        put16(out, 0x0002);
        put16(out, 12);
        put32(out, static_cast<uint32_t>(12 + values_pool.size() + package.size()));
        put32(out, 1); // package count
        out.insert(out.end(), values_pool.begin(), values_pool.end());
        out.insert(out.end(), package.begin(), package.end());
        return out;
    }

  private:
    struct Value
    {
        uint16_t entry;
        uint8_t type;
        uint32_t data;
    };

    static void put16(std::vector<uint8_t> &out, uint32_t v)
    {
        out.push_back(static_cast<uint8_t>(v));
        out.push_back(static_cast<uint8_t>(v >> 8));
    }
    static void put32(std::vector<uint8_t> &out, uint32_t v)
    {
        for (int shift = 0; shift < 32; shift += 8)
            out.push_back(static_cast<uint8_t>(v >> shift));
    }

    /** A UTF-8 string pool: each string is its UTF-16 length, its byte
     *  length, the bytes and a NUL.  The test strings are short ASCII. */
    static std::vector<uint8_t> pool(const std::vector<std::string> &strings)
    {
        std::vector<uint8_t> data;
        std::vector<uint32_t> offsets;
        for (const std::string &text : strings) {
            offsets.push_back(static_cast<uint32_t>(data.size()));
            data.push_back(static_cast<uint8_t>(text.size()));
            data.push_back(static_cast<uint8_t>(text.size()));
            data.insert(data.end(), text.begin(), text.end());
            data.push_back(0);
        }
        while (data.size() % 4 != 0)
            data.push_back(0);
        const uint32_t header = 28;
        const uint32_t strings_start = header + 4 * static_cast<uint32_t>(strings.size());
        std::vector<uint8_t> out;
        put16(out, 0x0001);
        put16(out, header);
        put32(out, strings_start + static_cast<uint32_t>(data.size()));
        put32(out, static_cast<uint32_t>(strings.size()));
        put32(out, 0);     // styles
        put32(out, 0x100); // UTF8_FLAG
        put32(out, strings_start);
        put32(out, 0);
        for (uint32_t offset : offsets)
            put32(out, offset);
        out.insert(out.end(), data.begin(), data.end());
        return out;
    }

    std::vector<std::string> strings_;
    std::map<ArscConfig, std::vector<Value>> configs_;
    std::map<uint16_t, uint32_t> spec_flags_;
    uint32_t entry_count_ = 0;
};

} // namespace dex::test
