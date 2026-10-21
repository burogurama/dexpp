#pragma once

// A description of a program as the analysis layer sees it — classes,
// members, flags, annotations, initial values, calls, field accesses,
// strings, type uses and exception handlers — without offsets or pool
// indices.  Two DEX files that hold the same program describe the same.

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <span>
#include <string>
#include <vector>

#include "dex.hpp"

inline std::string render(const dex::AnnotationValue &v);

inline std::string render(const dex::Annotation &a)
{
    std::string out = std::format("@{}/{}(", a.type_descriptor, static_cast<int>(a.visibility));
    for (const auto &e : a.elements)
        out += e.name + "=" + render(e.value) + ",";
    return out + ")";
}

inline std::string render(const dex::AnnotationValue &v)
{
    using V = dex::AnnotationValue;
    return std::visit(
        [](const auto &x) -> std::string {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>)
                return "null";
            else if constexpr (std::is_same_v<T, bool>)
                return x ? "true" : "false";
            else if constexpr (std::is_same_v<T, int64_t>)
                return std::to_string(x);
            else if constexpr (std::is_same_v<T, double>)
                return std::format("{}", std::bit_cast<uint64_t>(x));
            else if constexpr (std::is_same_v<T, std::string>)
                return "\"" + x + "\"";
            else if constexpr (std::is_same_v<T, V::TypeRef>)
                return x.descriptor;
            else if constexpr (std::is_same_v<T, V::EnumRef>)
                return "enum " + x.constant.class_descriptor + x.constant.name;
            else if constexpr (std::is_same_v<T, V::FieldRef>)
                return "field " + x.field.class_descriptor + x.field.name + x.field.type;
            else if constexpr (std::is_same_v<T, V::MethodRef>)
                return "method " + x.method.class_descriptor + x.method.name + x.method.proto;
            else if constexpr (std::is_same_v<T, V::MethodTypeRef>)
                return "methodtype " + x.proto;
            else if constexpr (std::is_same_v<T, V::MethodHandleRef>)
                return "methodhandle"; // a pool index: differs between files
            else if constexpr (std::is_same_v<T, std::vector<V>>) {
                std::string out = "[";
                for (const V &e : x)
                    out += render(e) + ",";
                return out + "]";
            }
            else
                return render(*x);
        },
        v.value);
}

/** Everything the analysis layer sees in a program, without offsets, as
 *  sorted lines. */
inline std::vector<std::string> describe(const dex::AnalysisContext &ctx)
{
    std::vector<std::string> out;
    for (const dex::Class &c : ctx.classes()) {
        std::string cls = std::string(c.name());
        std::string head =
            std::format("class {} {:x}", cls, static_cast<uint32_t>(c.access_flags()));
        if (auto s = c.superclass())
            head += " extends " + std::string(s->descriptor());
        for (const auto &i : c.interfaces())
            head += " implements " + std::string(i.descriptor());
        if (auto f = c.source_file())
            head += " source " + std::string(*f);
        out.push_back(head);
        for (const auto &a : c.annotations())
            out.push_back(cls + " " + render(a));
        for (const dex::Field &f : c.fields()) {
            std::string line =
                std::format("{} field {}:{} {:x}", cls, f.name(), f.type().descriptor(),
                            static_cast<uint32_t>(f.access_flags()));
            if (auto v = f.initial_value())
                line += " = " + render(*v);
            for (const auto &a : f.annotations())
                line += " " + render(a);
            out.push_back(line);
        }
        for (const dex::Method &m : c.methods()) {
            std::string sig = std::string(m.name()) + "(";
            for (const auto &p : m.parameters())
                sig += p.descriptor();
            sig += ")" + std::string(m.return_type().descriptor());
            std::string head =
                std::format("{} method {} {:x}", cls, sig, static_cast<uint32_t>(m.access_flags()));
            for (const auto &a : m.annotations())
                head += " " + render(a);
            out.push_back(head);
            for (const auto &call : m.calls())
                out.push_back(std::format("{}.{} calls {}->{}{} {}", cls, sig,
                                          call.target.class_descriptor, call.target.name,
                                          call.target.proto, static_cast<int>(call.kind)));
            for (const auto &fa : m.field_accesses())
                out.push_back(std::format("{}.{} {} {}.{}:{}", cls, sig,
                                          fa.is_write ? "writes" : "reads",
                                          fa.field.class_descriptor, fa.field.name, fa.field.type));
            for (const auto &s : m.string_loads())
                out.push_back(std::format("{}.{} loads \"{}\"", cls, sig, s.value));
            for (const auto &t : m.type_uses())
                out.push_back(std::format("{}.{} uses {} {}", cls, sig, t.descriptor,
                                          static_cast<int>(t.kind)));
            for (const auto &t : m.try_blocks()) {
                std::string line = cls + "." + sig + " try";
                for (const auto &h : t.handlers)
                    line += " " + std::string(h.type_descriptor);
                if (t.catch_all_offset)
                    line += " finally";
                out.push_back(line);
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

/** Write @p bytes to a temporary file and return its path. */
inline std::string temp_file(const std::string &tag, std::span<const uint8_t> bytes)
{
    static const unsigned run = std::random_device{}();
    auto path =
        std::filesystem::temp_directory_path() / std::format("dexpp_model_{:08x}_{}.dex", run, tag);
    std::ofstream(path, std::ios::binary)
        .write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    return path.string();
}

inline std::vector<std::string> describe_file(const std::string &path)
{
    auto ctx = dex::AnalysisContext::from_dex(path);
    EXPECT_TRUE(ctx.has_value()) << path;
    return ctx ? describe(*ctx) : std::vector<std::string>{};
}
