#include "class_hierarchy.hpp"

#include <queue>
#include <unordered_set>
#include <utility>

#include "access_flags.hpp"
#include "analysis_context_impl.hpp"
#include "raw/parser.hpp"

namespace dex {

namespace {

constexpr uint32_t NO_INDEX = 0xFFFFFFFF;

} // namespace

const ClassHierarchy::Node *ClassHierarchy::find(std::string_view descriptor) const
{
    auto it = nodes_.find(descriptor);
    if (it == nodes_.end())
        return nullptr;
    return &it->second;
}

std::vector<std::string_view> ClassHierarchy::supertypes(std::string_view descriptor) const
{
    const Node *n = find(descriptor);
    if (!n)
        return {};
    std::vector<std::string_view> out;
    out.reserve((n->superclass.has_value() ? 1 : 0) + n->interfaces.size());
    if (n->superclass.has_value())
        out.emplace_back(*n->superclass);
    for (const auto &i : n->interfaces)
        out.emplace_back(i);
    return out;
}

std::optional<std::string_view> ClassHierarchy::superclass_of(std::string_view descriptor) const
{
    const Node *n = find(descriptor);
    if (!n || !n->superclass.has_value())
        return std::nullopt;
    return std::string_view(*n->superclass);
}

std::vector<std::string_view> ClassHierarchy::direct_interfaces(std::string_view descriptor) const
{
    const Node *n = find(descriptor);
    if (!n)
        return {};
    std::vector<std::string_view> out;
    out.reserve(n->interfaces.size());
    for (const auto &i : n->interfaces)
        out.emplace_back(i);
    return out;
}

std::vector<std::string_view> ClassHierarchy::subclasses(std::string_view descriptor) const
{
    const Node *n = find(descriptor);
    if (!n)
        return {};
    std::vector<std::string_view> out;
    out.reserve(n->subclasses.size());
    for (const auto &s : n->subclasses)
        out.emplace_back(s);
    return out;
}

std::vector<std::string_view> ClassHierarchy::implementers(std::string_view descriptor) const
{
    const Node *n = find(descriptor);
    if (!n)
        return {};
    std::vector<std::string_view> out;
    out.reserve(n->implementers.size());
    for (const auto &i : n->implementers)
        out.emplace_back(i);
    return out;
}

std::vector<std::string_view> ClassHierarchy::all_supertypes(std::string_view descriptor) const
{
    const Node *start = find(descriptor);
    if (!start)
        return {};

    std::vector<std::string_view> out;
    std::unordered_set<std::string_view> seen;
    std::queue<const Node *> q;

    auto enqueue = [&](std::string_view sv, const Node *n) {
        if (!n || !seen.insert(sv).second)
            return;
        out.push_back(sv);
        q.push(n);
    };

    if (start->superclass.has_value())
        enqueue(*start->superclass, find(*start->superclass));
    for (const auto &i : start->interfaces)
        enqueue(i, find(i));

    while (!q.empty()) {
        const Node *cur = q.front();
        q.pop();
        if (cur->superclass.has_value())
            enqueue(*cur->superclass, find(*cur->superclass));
        for (const auto &i : cur->interfaces)
            enqueue(i, find(i));
    }
    return out;
}

std::vector<std::string_view> ClassHierarchy::all_descendants(std::string_view descriptor) const
{
    const Node *start = find(descriptor);
    if (!start)
        return {};

    std::vector<std::string_view> out;
    std::unordered_set<std::string_view> seen;
    std::queue<const Node *> q;

    auto enqueue = [&](std::string_view sv, const Node *n) {
        if (!n || !seen.insert(sv).second)
            return;
        out.push_back(sv);
        q.push(n);
    };

    for (const auto &s : start->subclasses)
        enqueue(s, find(s));
    for (const auto &i : start->implementers)
        enqueue(i, find(i));

    while (!q.empty()) {
        const Node *cur = q.front();
        q.pop();
        for (const auto &s : cur->subclasses)
            enqueue(s, find(s));
        for (const auto &i : cur->implementers)
            enqueue(i, find(i));
    }
    return out;
}

bool ClassHierarchy::is_subtype_of(std::string_view sub, std::string_view super) const
{
    if (sub == super)
        return find(sub) != nullptr; // reflexive only if known to the graph
    auto supers = all_supertypes(sub);
    for (auto sv : supers)
        if (sv == super)
            return true;
    return false;
}

bool ClassHierarchy::is_loaded(std::string_view descriptor) const
{
    const Node *n = find(descriptor);
    return n && n->loaded;
}

bool ClassHierarchy::is_interface(std::string_view descriptor) const
{
    const Node *n = find(descriptor);
    return n && n->is_iface;
}

ClassHierarchy build_class_hierarchy(const detail::AnalysisContextImpl &impl)
{
    ClassHierarchy h;

    // Pass 1: register every loaded class with its direct supertypes; ensure
    // unloaded supertypes referenced by loaded classes have placeholder nodes.
    for (std::size_t i = 0; i < impl.dex_files().size(); ++i) {
        const auto &dex = impl.dex_files()[i];
        for (uint32_t j = 0; j < static_cast<uint32_t>(dex.class_defs().size()); ++j) {
            const auto &cdef = dex.class_defs()[j];
            std::string desc(impl.type_descriptor_of(i, cdef.class_idx));

            auto &node = h.nodes_[desc];
            node.loaded = true;
            node.is_iface =
                has_flag(static_cast<AccessFlags>(cdef.access_flags), AccessFlags::Interface);

            if (cdef.superclass_idx != NO_INDEX) {
                std::string super_desc(impl.type_descriptor_of(i, cdef.superclass_idx));
                node.superclass = super_desc;
                h.nodes_.try_emplace(super_desc); // placeholder if unloaded
            }

            if (cdef.interfaces_off != 0) {
                auto type_list =
                    raw::parser::parse_type_list(impl.buffer_of(i), cdef.interfaces_off);
                if (type_list.has_value()) {
                    node.interfaces.reserve(type_list->type_ids.size());
                    for (auto type_idx : type_list->type_ids) {
                        std::string iface_desc(impl.type_descriptor_of(i, type_idx));
                        node.interfaces.push_back(iface_desc);
                        h.nodes_.try_emplace(iface_desc);
                    }
                }
            }
        }
    }

    // Pass 2: build inverse edges (subclasses, implementers).  We snapshot the
    // (sub, super) and (impl, iface) pairs first to avoid mutating the map
    // while iterating it.
    std::vector<std::pair<std::string, std::string>> sub_super;
    std::vector<std::pair<std::string, std::string>> impl_iface;
    for (const auto &[desc, node] : h.nodes_) {
        if (node.superclass.has_value())
            sub_super.emplace_back(desc, *node.superclass);
        for (const auto &iface : node.interfaces)
            impl_iface.emplace_back(desc, iface);
    }
    for (const auto &[sub, super] : sub_super)
        h.nodes_[super].subclasses.push_back(sub);
    for (const auto &[impl_desc, iface] : impl_iface)
        h.nodes_[iface].implementers.push_back(impl_desc);

    return h;
}

} // namespace dex
