#include "call_graph.hpp"

#include <set>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

#include "access_flags.hpp"
#include "analysis_context_impl.hpp"
#include "class_hierarchy.hpp"
#include "instruction.hpp"
#include "method.hpp"

namespace dex {

namespace {

// Standard hash combiner (boost-style xor-shift mixer).
inline void hash_combine(std::size_t &seed, std::size_t v) noexcept
{
    seed ^= v + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}

} // namespace

std::size_t MethodIdHash::operator()(const MethodId &m) const noexcept
{
    std::size_t h = std::hash<std::string>{}(m.class_descriptor);
    hash_combine(h, std::hash<std::string>{}(m.name));
    hash_combine(h, std::hash<std::string>{}(m.proto));
    return h;
}

std::optional<InvokeKind> invoke_kind_from_opcode(uint8_t opcode)
{
    switch (opcode) {
    case 0x6e:
    case 0x74:
        return InvokeKind::Virtual;
    case 0x6f:
    case 0x75:
        return InvokeKind::Super;
    case 0x70:
    case 0x76:
        return InvokeKind::Direct;
    case 0x71:
    case 0x77:
        return InvokeKind::Static;
    case 0x72:
    case 0x78:
        return InvokeKind::Interface;
    case 0xfa:
    case 0xfb:
        return InvokeKind::Polymorphic;
    case 0xfc:
    case 0xfd:
        return InvokeKind::Custom;
    default:
        return std::nullopt;
    }
}

const CallNode *CallGraph::find(const MethodId &id) const
{
    auto it = index_.find(id);
    if (it == index_.end())
        return nullptr;
    return &nodes_[it->second];
}

std::optional<Method> CallGraph::method_for(const CallNode &node) const
{
    if (node.index >= resolved_refs_.size() || !resolved_refs_[node.index].has_value())
        return std::nullopt;

    auto impl = impl_.lock();
    if (!impl)
        return std::nullopt;

    const auto &ref = *resolved_refs_[node.index];
    return Method(std::move(impl), ref.dex_idx, ref.method_abs_idx, ref.access_flags, ref.code_off);
}

CallGraph build_call_graph(std::shared_ptr<const detail::AnalysisContextImpl> impl)
{
    CallGraph g;
    g.impl_ = impl;

    if (!impl)
        return g;

    // Helper: get-or-create a node for a given MethodId.
    auto lookup_or_create = [&](const MethodId &id) -> uint32_t {
        if (auto it = g.index_.find(id); it != g.index_.end())
            return it->second;
        auto idx = static_cast<uint32_t>(g.nodes_.size());
        g.nodes_.push_back(CallNode{idx, id, /*resolved=*/false, {}, {}});
        g.resolved_refs_.emplace_back();
        g.index_.emplace(id, idx);
        return idx;
    };

    // A. Walk every defined method, register a resolved node, and remember its
    // (dex_idx, method_abs_idx) for step B.
    struct Defined
    {
        uint32_t node_idx;
        std::size_t dex_idx;
        uint32_t method_abs_idx;
        uint32_t code_off;
    };
    std::vector<Defined> defined;

    for (std::size_t i = 0; i < impl->dex_files().size(); ++i) {
        const auto &dex = impl->dex_files()[i];
        for (uint32_t j = 0; j < static_cast<uint32_t>(dex.class_defs().size()); ++j) {
            const auto &cdef = dex.class_defs()[j];
            const auto *cd = impl->get_class_data(i, cdef.class_data_off);
            if (!cd)
                continue;

            std::string declaring_descriptor(impl->type_descriptor_of(i, cdef.class_idx));

            auto register_method = [&](const raw::EncodedMethod &em) {
                const auto *mid_item = impl->method_item(i, em.method_idx);
                if (!mid_item)
                    return; // malformed class_data: skip this method
                MethodId id{
                    declaring_descriptor,
                    std::string(impl->string_of(i, mid_item->name_idx)),
                    impl->proto_descriptor_of(i, mid_item->proto_idx),
                };
                uint32_t node_idx = lookup_or_create(id);
                g.nodes_[node_idx].resolved = true;
                // Last-loaded wins on duplicate definitions.
                g.resolved_refs_[node_idx] =
                    CallGraph::ResolvedRef{i, em.method_idx, em.access_flags, em.code_off};
                if (em.code_off != 0)
                    defined.push_back({node_idx, i, em.method_idx, em.code_off});
            };

            for (const auto &em : cd->direct_methods)
                register_method(em);
            for (const auto &em : cd->virtual_methods)
                register_method(em);
        }
    }

    // B. For each defined method with code, walk its instructions and emit edges.
    for (const auto &d : defined) {
        auto insns = impl->get_instructions(d.dex_idx, d.code_off);
        for (const auto &insn : insns) {
            std::visit(
                [&](const auto &v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, InvokeInstruction>) {
                        // The variant is only emitted for known invoke opcodes,
                        // so kind is always present here.
                        auto kind = invoke_kind_from_opcode(v.base.opcode);
                        if (!kind.has_value())
                            return;
                        MethodId callee_mid = impl->method_id_of(d.dex_idx, v.method_index);
                        uint32_t callee_node = lookup_or_create(callee_mid);
                        g.edges_.push_back(CallEdge{d.node_idx, callee_node, *kind, v.base.offset});
                    }
                    else if constexpr (std::is_same_v<T, InvokeCustomInstruction>) {
                        // Prefix with dex_idx — call_site_index is per-DEX; without
                        // the prefix, two DEX files' call_site #0 collapse into one
                        // node and accumulate edges from both.
                        MethodId callee_mid{
                            "call_site",
                            "#" + std::to_string(d.dex_idx) + ":" +
                                std::to_string(v.call_site_index),
                            "",
                        };
                        uint32_t callee_node = lookup_or_create(callee_mid);
                        g.edges_.push_back(
                            CallEdge{d.node_idx, callee_node, InvokeKind::Custom, v.base.offset});
                    }
                },
                insn);
        }
    }

    // ---- Hierarchy-based resolution shared by B2 and B3 -------------------
    const ClassHierarchy &hierarchy = impl->get_class_hierarchy();

    // Linearized lookup order for a receiver descriptor: the descriptor itself
    // and its superclass chain (nearest first), then the deduped transitive
    // superinterfaces seeded from every chain class in order — JVMS 5.4.3.3
    // searches the superinterfaces of C, which includes interfaces implemented
    // anywhere up the chain.  Both walks are cycle-guarded (a malformed DEX can
    // encode `A extends B extends A`; an unguarded walk is a hang).  Owned
    // strings: node/hierarchy storage must not be aliased across map growth.
    struct Linearization
    {
        std::vector<std::string> chain;
        std::vector<std::string> interfaces;
    };
    std::unordered_map<std::string, Linearization> lin_memo;
    auto linearize = [&](std::string_view desc) -> const Linearization & {
        std::string key(desc);
        if (auto it = lin_memo.find(key); it != lin_memo.end())
            return it->second;
        Linearization lin;
        std::unordered_set<std::string_view> seen;
        for (std::optional<std::string_view> d = desc; d.has_value();
             d = hierarchy.superclass_of(*d)) {
            if (!seen.insert(*d).second)
                break;
            lin.chain.emplace_back(*d);
        }
        std::unordered_set<std::string> iface_seen;
        for (const auto &cls : lin.chain)
            for (auto i : hierarchy.direct_interfaces(cls))
                if (iface_seen.insert(std::string(i)).second)
                    lin.interfaces.emplace_back(i);
        for (std::size_t qi = 0; qi < lin.interfaces.size(); ++qi)
            for (auto i : hierarchy.direct_interfaces(lin.interfaces[qi]))
                if (iface_seen.insert(std::string(i)).second)
                    lin.interfaces.emplace_back(i);
        return lin_memo.emplace(std::move(key), std::move(lin)).first->second;
    };

    // Nearest *resolved* definition of (name, proto) visible from `desc`.
    // Resolved-only is essential: the index also holds unresolved nodes for
    // every referenced method_id (e.g. a stray `invoke-virtual Mid.foo`
    // elsewhere), and stopping at one would recreate the dangling-edge problem
    // this walk exists to fix.
    //
    // Priority: concrete chain match > interface default method > abstract
    // chain match > abstract interface declaration.  The chain scan stops at
    // its first match either way — an abstract redeclaration shadows concrete
    // ancestors.  static_walk restricts to the chain and to static targets:
    // static interface methods are never inherited (ART throws ICCE).
    struct ResolveKey
    {
        std::string desc, name, proto;
        bool static_walk;
        bool operator==(const ResolveKey &) const = default;
    };
    struct ResolveKeyHash
    {
        std::size_t operator()(const ResolveKey &k) const noexcept
        {
            std::size_t h = std::hash<std::string>{}(k.desc);
            hash_combine(h, std::hash<std::string>{}(k.name));
            hash_combine(h, std::hash<std::string>{}(k.proto));
            hash_combine(h, k.static_walk ? 1 : 0);
            return h;
        }
    };
    std::unordered_map<ResolveKey, std::optional<uint32_t>, ResolveKeyHash> resolve_memo;

    auto resolve_up = [&](std::string_view desc, const std::string &name, const std::string &proto,
                          bool static_walk) -> std::optional<uint32_t> {
        ResolveKey key{std::string(desc), name, proto, static_walk};
        if (auto it = resolve_memo.find(key); it != resolve_memo.end())
            return it->second;

        auto match_at = [&](const std::string &cls, bool &is_abstract) -> std::optional<uint32_t> {
            auto it = g.index_.find(MethodId{cls, name, proto});
            if (it == g.index_.end())
                return std::nullopt;
            const auto &ref = g.resolved_refs_[it->second];
            if (!ref.has_value())
                return std::nullopt;
            auto flags = static_cast<AccessFlags>(ref->access_flags);
            if (has_flag(flags, AccessFlags::Private) || has_flag(flags, AccessFlags::Constructor))
                return std::nullopt;
            if (has_flag(flags, AccessFlags::Static) != static_walk)
                return std::nullopt;
            is_abstract = has_flag(flags, AccessFlags::Abstract);
            return it->second;
        };

        const Linearization &lin = linearize(desc);
        std::optional<uint32_t> result;
        std::optional<uint32_t> abstract_fallback;

        for (const auto &cls : lin.chain) {
            bool is_abstract = false;
            if (auto m = match_at(cls, is_abstract)) {
                if (!is_abstract)
                    result = m;
                else
                    abstract_fallback = m;
                break;
            }
        }
        if (!result.has_value() && !static_walk) {
            std::optional<uint32_t> iface_abstract;
            for (const auto &iface : lin.interfaces) {
                bool is_abstract = false;
                if (auto m = match_at(iface, is_abstract)) {
                    if (!is_abstract) { // default method
                        result = m;
                        break;
                    }
                    if (!iface_abstract.has_value())
                        iface_abstract = m;
                }
            }
            if (!result.has_value())
                result = abstract_fallback.has_value() ? abstract_fallback : iface_abstract;
        }
        else if (!result.has_value()) {
            result = abstract_fallback;
        }

        resolve_memo.emplace(std::move(key), result);
        return result;
    };

    // B2. Class-Hierarchy Analysis over virtual/interface call sites: one
    // ChaOverride edge per distinct dispatch target among the declared class's
    // loaded descendants.  A descendant defining the method contributes its
    // own definition (resolve_up from the descendant is self-inclusive); one
    // that doesn't contributes what it inherits — this pulls in
    // implementations from outside the declared class's subtree (`class C
    // extends Base implements I` dispatching I.m to Base.m).  The call site's
    // own resolution is excluded: it is already reachable via the Declared or
    // InheritedResolution edge.  Targets are emitted sorted by node index —
    // all_descendants order is hash-map-iteration dependent.  Memoized per
    // declared node; many call sites share one declared target.
    std::unordered_map<uint32_t, std::vector<uint32_t>> override_memo;
    auto overrides_of = [&](uint32_t declared_node) -> const std::vector<uint32_t> & {
        if (auto it = override_memo.find(declared_node); it != override_memo.end())
            return it->second;
        const MethodId id = g.nodes_[declared_node].id;
        std::optional<uint32_t> own = resolve_up(id.class_descriptor, id.name, id.proto, false);
        std::set<uint32_t> targets;
        for (auto desc : hierarchy.all_descendants(id.class_descriptor)) {
            auto t = resolve_up(desc, id.name, id.proto, false);
            if (t.has_value() && *t != declared_node && t != own)
                targets.insert(*t);
        }
        std::vector<uint32_t> out(targets.begin(), targets.end());
        return override_memo.emplace(declared_node, std::move(out)).first->second;
    };

    std::vector<CallEdge> cha_edges;
    for (const auto &e : g.edges_) {
        if (e.kind != InvokeKind::Virtual && e.kind != InvokeKind::Interface)
            continue;
        for (uint32_t target : overrides_of(e.callee_node))
            cha_edges.push_back(
                CallEdge{e.caller_node, target, e.kind, e.code_offset, EdgeOrigin::ChaOverride});
    }
    g.edges_.insert(g.edges_.end(), cha_edges.begin(), cha_edges.end());

    // B3. Upward resolution: a declared target no loaded class defines gets one
    // InheritedResolution edge to what Dalvik resolution would find.  Runs
    // after B2 and scans Declared edges only — fanning CHA out over the
    // resolution target's descendants would wrongly pull in overrides from
    // sibling branches of the declared class.
    std::vector<CallEdge> inherited_edges;
    for (const auto &e : g.edges_) {
        if (e.origin != EdgeOrigin::Declared)
            continue;
        if (e.kind != InvokeKind::Virtual && e.kind != InvokeKind::Interface &&
            e.kind != InvokeKind::Super && e.kind != InvokeKind::Static)
            continue;
        const CallNode &callee = g.nodes_[e.callee_node];
        if (callee.resolved)
            continue;
        auto t = resolve_up(callee.id.class_descriptor, callee.id.name, callee.id.proto,
                            e.kind == InvokeKind::Static);
        if (t.has_value())
            inherited_edges.push_back(CallEdge{e.caller_node, *t, e.kind, e.code_offset,
                                               EdgeOrigin::InheritedResolution});
    }
    g.edges_.insert(g.edges_.end(), inherited_edges.begin(), inherited_edges.end());

    // C. Populate node out/in lists from edges.
    for (uint32_t e_idx = 0; e_idx < static_cast<uint32_t>(g.edges_.size()); ++e_idx) {
        const auto &e = g.edges_[e_idx];
        g.nodes_[e.caller_node].outgoing.push_back(e_idx);
        g.nodes_[e.callee_node].incoming.push_back(e_idx);
    }

    return g;
}

} // namespace dex
