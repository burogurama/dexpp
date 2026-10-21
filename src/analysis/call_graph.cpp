#include "call_graph.hpp"

#include <algorithm>
#include <atomic>
#include <deque>
#include <exception>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>

#include "access_flags.hpp"
#include "analysis_context_impl.hpp"
#include "invoke_sites.hpp"
#include "method.hpp"
#include "raw/parser.hpp"

namespace dex {

namespace {

// True for the edges whose callee's dispatch targets apply at their call site.
bool dispatches(const CallEdge &e)
{
    return e.origin == EdgeOrigin::Declared &&
           (e.kind == InvokeKind::Virtual || e.kind == InvokeKind::Interface);
}

// Row `node` of a compressed-row table; empty when `node` is out of range.
std::span<const uint32_t> row(const std::vector<std::size_t> &offsets,
                              const std::vector<uint32_t> &values, uint32_t node)
{
    if (static_cast<std::size_t>(node) + 1 >= offsets.size())
        return {};
    return std::span(values).subspan(offsets[node], offsets[node + 1] - offsets[node]);
}

void sort_unique(std::vector<uint32_t> &v)
{
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
}

constexpr uint32_t kNone = 0xFFFFFFFF;

uint64_t pack(uint32_t hi, uint32_t lo) { return (static_cast<uint64_t>(hi) << 32) | lo; }

// Open-addressing map from a 64-bit key to a 32-bit value.  The builder's id
// tables hold hundreds of thousands of entries; std::unordered_map would
// allocate a node for each one.  A key and its value share a slot, so a probe
// touches one cache line.  Keys are pack()ed ids, never ~0.
class FlatMap
{
  public:
    // The value stored under `key`, after storing `value` there if the key was
    // absent; `second` is true when it was.
    std::pair<uint32_t, bool> try_emplace(uint64_t key, uint32_t value)
    {
        if ((size_ + 1) * 2 > slots_.size())
            grow();
        for (std::size_t i = slot_of(key);; i = (i + 1) & (slots_.size() - 1)) {
            if (slots_[i].key == key)
                return {slots_[i].value, false};
            if (slots_[i].key == kEmptyKey) {
                slots_[i] = Slot{key, value};
                ++size_;
                return {value, true};
            }
        }
    }

  private:
    static constexpr uint64_t kEmptyKey = ~0ULL;

    struct Slot
    {
        uint64_t key = kEmptyKey;
        uint32_t value = 0;
    };

    std::size_t slot_of(uint64_t key) const
    {
        // murmur3 finalizer: spreads pack()ed ids, whose low bits are dense.
        key ^= key >> 33;
        key *= 0xff51afd7ed558ccdULL;
        key ^= key >> 33;
        key *= 0xc4ceb9fe1a85ec53ULL;
        key ^= key >> 33;
        return static_cast<std::size_t>(key) & (slots_.size() - 1);
    }

    void grow()
    {
        std::vector<Slot> old(std::max<std::size_t>(64, slots_.size() * 2));
        old.swap(slots_);
        for (const Slot &slot : old) {
            if (slot.key == kEmptyKey)
                continue;
            std::size_t j = slot_of(slot.key);
            while (slots_[j].key != kEmptyKey)
                j = (j + 1) & (slots_.size() - 1);
            slots_[j] = slot;
        }
    }

    std::vector<Slot> slots_;
    std::size_t size_ = 0;
};

// Gives every distinct string a dense integer id (open addressing, like
// FlatMap).  Keys are views, so each interned string must outlive the build:
// they come from the context's string cache, from the per-DEX preparation, or
// from builder-owned storage, all of which do.
class Interner
{
  public:
    static std::size_t hash(std::string_view s) { return std::hash<std::string_view>{}(s); }

    uint32_t id(std::string_view s) { return id(s, hash(s)); }

    // id(s) when hash(s) is already known (computed on a worker thread).
    uint32_t id(std::string_view s, std::size_t h)
    {
        if (s.empty())
            s = std::string_view(""); // one id for every empty string, never a null view
        if ((views_.size() + 1) * 2 > slots_.size())
            grow();
        for (std::size_t i = h & (slots_.size() - 1);; i = (i + 1) & (slots_.size() - 1)) {
            uint32_t slot = slots_[i];
            if (slot == 0) {
                views_.push_back(s);
                hashes_.push_back(h);
                slots_[i] = static_cast<uint32_t>(views_.size());
                return slots_[i] - 1;
            }
            if (hashes_[slot - 1] == h && views_[slot - 1] == s)
                return slot - 1;
        }
    }
    std::string_view view(uint32_t id) const { return views_[id]; }
    std::size_t size() const { return views_.size(); }

  private:
    void grow()
    {
        slots_.assign(std::max<std::size_t>(64, slots_.size() * 2), 0); // 0 = empty, else id + 1
        for (uint32_t id = 0; id < views_.size(); ++id) {
            std::size_t i = hashes_[id] & (slots_.size() - 1);
            while (slots_[i] != 0)
                i = (i + 1) & (slots_.size() - 1);
            slots_[i] = id + 1;
        }
    }

    std::vector<uint32_t> slots_;
    std::vector<std::string_view> views_;
    std::vector<std::size_t> hashes_;
};

// Run fn(i) for every i in `order` on up to `threads` threads, the calling
// thread included.  An exception thrown by fn is kept and rethrown here once
// every thread has stopped — escaping a std::thread, it would std::terminate
// the process; when several throw, the one for the lowest i wins.
template <typename Fn>
void for_each_parallel(const std::vector<std::size_t> &order, std::size_t threads, Fn &&fn)
{
    std::vector<std::exception_ptr> errors(order.size());
    std::atomic<std::size_t> next{0};
    auto work = [&] {
        for (std::size_t k = next.fetch_add(1); k < order.size(); k = next.fetch_add(1)) {
            try {
                fn(order[k]);
            }
            catch (...) {
                errors[order[k]] = std::current_exception();
            }
        }
    };
    {
        std::vector<std::jthread> pool;
        try {
            for (std::size_t t = 1; t < threads; ++t)
                pool.emplace_back(work);
        }
        catch (const std::exception &) {
            // Could not start every thread (resource limits): the threads
            // that did start, plus this one, still drain the queue.
        }
        work();
    } // the pool joins here
    for (const auto &error : errors)
        if (error)
            std::rethrow_exception(error);
}

// A compressed-row table: row r is values[offsets[r] .. offsets[r + 1]).
struct Rows
{
    std::vector<std::size_t> offsets;
    std::vector<uint32_t> values;
};

// Group `items` by key into compressed rows (a counting sort that keeps each
// group in item order).
template <typename Key>
Rows group_by(std::size_t key_count, const std::vector<uint32_t> &items, Key &&key_of)
{
    Rows rows;
    rows.offsets.assign(key_count + 1, 0);
    for (uint32_t item : items)
        ++rows.offsets[static_cast<std::size_t>(key_of(item)) + 1];
    for (std::size_t k = 0; k < key_count; ++k)
        rows.offsets[k + 1] += rows.offsets[k];
    rows.values.resize(items.size());
    std::vector<std::size_t> next_free(rows.offsets.begin(), rows.offsets.end() - 1);
    for (uint32_t item : items)
        rows.values[next_free[key_of(item)]++] = item;
    return rows;
}

} // namespace

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
    if (index_.empty())
        return nullptr;
    const std::size_t mask = index_.size() - 1;
    for (std::size_t i = MethodIdHash{}(id)&mask;; i = (i + 1) & mask) {
        uint32_t slot = index_[i];
        if (slot == 0)
            return nullptr;
        if (nodes_[slot - 1].id == id)
            return &nodes_[slot - 1];
    }
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

std::span<const uint32_t> CallGraph::dispatch_targets(uint32_t node) const
{
    return row(dispatch_target_offsets_, dispatch_target_values_, node);
}

std::span<const uint32_t> CallGraph::dispatch_targets(const CallEdge &edge) const
{
    if (!dispatches(edge))
        return {};
    return dispatch_targets(edge.callee_node);
}

std::span<const uint32_t> CallGraph::dispatch_sources(uint32_t node) const
{
    return row(dispatch_source_offsets_, dispatch_source_values_, node);
}

std::vector<uint32_t> CallGraph::callees(uint32_t node) const
{
    std::vector<uint32_t> out;
    if (node >= nodes_.size())
        return out;
    // Many sites can declare the same target; add each target's dispatch list
    // once rather than once per site.
    std::vector<uint32_t> dispatched;
    for (uint32_t e_idx : nodes_[node].outgoing) {
        const CallEdge &e = edges_[e_idx];
        out.push_back(e.callee_node);
        if (dispatches(e))
            dispatched.push_back(e.callee_node);
    }
    sort_unique(dispatched);
    for (uint32_t declared : dispatched) {
        auto targets = dispatch_targets(declared);
        out.insert(out.end(), targets.begin(), targets.end());
    }
    sort_unique(out);
    return out;
}

std::vector<uint32_t> CallGraph::callers(uint32_t node) const
{
    std::vector<uint32_t> out;
    if (node >= nodes_.size())
        return out;
    for (uint32_t e_idx : nodes_[node].incoming)
        out.push_back(edges_[e_idx].caller_node);
    for (uint32_t declared : dispatch_sources(node))
        for (uint32_t e_idx : nodes_[declared].incoming)
            if (dispatches(edges_[e_idx]))
                out.push_back(edges_[e_idx].caller_node);
    sort_unique(out);
    return out;
}

CallGraph build_call_graph(std::shared_ptr<const detail::AnalysisContextImpl> impl)
{
    CallGraph g;
    g.impl_ = impl;

    if (!impl)
        return g;

    const std::size_t dex_count = impl->dex_files().size();
    // hardware_concurrency() reads system files on every call; ask once.
    static const unsigned hardware_threads = std::max(1u, std::thread::hardware_concurrency());
    const std::size_t threads = impl->threads != 0 ? impl->threads : hardware_threads;

    // ---- Per-DEX preparation, one thread per DEX file -----------------------
    // Everything the steps below need from one DEX file is independent of the
    // other DEX files: its methods and their call sites, its interface lists,
    // and the strings the steps intern — decoded, with their hashes.  So each
    // DEX file is prepared on its own thread.  Everything after this runs on
    // the calling thread in a fixed order, so node numbering and edge order do
    // not depend on the number of threads.
    struct DexPrep
    {
        // Every class_data method with a valid method_id, in class_def order,
        // direct then virtual; its call sites are sites[sites_begin, sites_end).
        struct Method
        {
            uint32_t class_idx;
            uint32_t method_idx;
            uint32_t access_flags;
            uint32_t code_off;
            std::size_t sites_begin;
            std::size_t sites_end;
        };
        std::vector<Method> methods;
        std::vector<detail::InvokeSite> sites;
        // Interface type indices of class_def j: iface_types[iface_offsets[j],
        // iface_offsets[j + 1]); none when the list does not parse.
        std::vector<std::size_t> iface_offsets;
        std::vector<uint32_t> iface_types;
        // Strings to intern and their hashes: the descriptor of each type id,
        // the name of each method_id, the descriptor of each proto id.
        std::vector<std::string_view> type_view, method_name;
        std::vector<std::size_t> type_hash, method_name_hash;
        std::vector<std::string> proto;
        std::vector<std::size_t> proto_hash;
    };
    std::vector<DexPrep> prep(dex_count);

    auto prepare = [&](std::size_t i) {
        const auto &dex = impl->dex_files()[i];
        DexPrep &p = prep[i];

        const auto type_count = static_cast<uint32_t>(dex.type_ids().size());
        p.type_view.resize(type_count);
        p.type_hash.resize(type_count);
        for (uint32_t t = 0; t < type_count; ++t) {
            p.type_view[t] = impl->type_descriptor_of(i, t);
            p.type_hash[t] = Interner::hash(p.type_view[t]);
        }
        const auto method_count = static_cast<uint32_t>(dex.method_ids().size());
        p.method_name.resize(method_count);
        p.method_name_hash.resize(method_count);
        for (uint32_t m = 0; m < method_count; ++m) {
            p.method_name[m] = impl->string_of(i, dex.method_ids()[m].name_idx);
            p.method_name_hash[m] = Interner::hash(p.method_name[m]);
        }
        const auto proto_count = static_cast<uint32_t>(dex.proto_ids().size());
        p.proto.resize(proto_count);
        p.proto_hash.resize(proto_count);
        for (uint32_t k = 0; k < proto_count; ++k) {
            p.proto[k] = impl->build_proto_descriptor(i, k);
            p.proto_hash[k] = Interner::hash(p.proto[k]);
        }

        p.iface_offsets.reserve(dex.class_defs().size() + 1);
        for (const auto &cdef : dex.class_defs()) {
            p.iface_offsets.push_back(p.iface_types.size());
            if (cdef.interfaces_off != 0) {
                auto list = raw::parser::parse_type_list(impl->buffer_of(i), cdef.interfaces_off);
                if (list.has_value())
                    p.iface_types.insert(p.iface_types.end(), list->type_ids.begin(),
                                         list->type_ids.end());
            }

            const auto *cd = impl->get_class_data(i, cdef.class_data_off);
            if (!cd)
                continue;
            for (const auto *methods : {&cd->direct_methods, &cd->virtual_methods})
                for (const auto &em : *methods) {
                    if (!impl->method_item(i, em.method_idx))
                        continue; // malformed class_data: skip this method
                    std::size_t begin = p.sites.size();
                    if (em.code_off != 0)
                        if (const raw::CodeItem *ci = impl->get_code_item(i, em.code_off))
                            detail::find_invokes(ci->insns, p.sites);
                    p.methods.push_back(DexPrep::Method{cdef.class_idx, em.method_idx,
                                                        em.access_flags, em.code_off, begin,
                                                        p.sites.size()});
                }
        }
        p.iface_offsets.push_back(p.iface_types.size());
    };

    {
        // Biggest DEX files first, so a large one picked up last does not keep
        // the other threads idle while it finishes.
        std::vector<std::size_t> order(dex_count);
        for (std::size_t i = 0; i < dex_count; ++i)
            order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return impl->buffer_of(a).size() > impl->buffer_of(b).size();
        });
        // Threads only pay off when the DEX files besides the biggest hold real
        // work: for a few small ones, starting a thread costs more than the
        // whole preparation (measured: ~10 µs serial vs ~80 µs threaded).
        constexpr std::size_t kMinParallelBytes = std::size_t{1} << 20;
        std::size_t besides_biggest = 0;
        for (std::size_t k = 1; k < dex_count; ++k)
            besides_biggest += impl->buffer_of(order[k]).size();
        std::size_t prepare_threads =
            besides_biggest >= kMinParallelBytes ? std::min(threads, dex_count) : 1;
        for_each_parallel(order, prepare_threads, prepare);
    }

    // ---- Interning ----------------------------------------------------------
    // Nodes are keyed by (class id, signature id), where a signature is a
    // (name id, prototype id) pair, rather than by MethodId string triples:
    // every lookup below is then an integer hash, and a node's MethodId strings
    // are built once, when the node is created.  Equal strings get equal ids, so
    // node identity is exactly MethodId equality.
    Interner classes, names, protos;
    FlatMap sig_ids;                                      // (name, proto) -> sig
    std::vector<std::pair<uint32_t, uint32_t>> sig_parts; // sig -> (name, proto)
    FlatMap node_ids;                                     // (class, sig) -> node
    std::vector<uint32_t> node_class;                     // parallel to g.nodes_
    std::vector<uint32_t> node_sig;                       // parallel to g.nodes_
    std::deque<std::string> owned;                        // synthetic call_site names

    auto sig_of = [&](uint32_t name, uint32_t proto) -> uint32_t {
        auto [sig, inserted] =
            sig_ids.try_emplace(pack(name, proto), static_cast<uint32_t>(sig_parts.size()));
        if (inserted)
            sig_parts.emplace_back(name, proto);
        return sig;
    };

    // Get-or-create the node for (cls, sig).  Its MethodId strings are filled
    // in once every node exists (see "Node ids" below).
    auto node_of = [&](uint32_t cls, uint32_t sig) -> uint32_t {
        auto [node, inserted] =
            node_ids.try_emplace(pack(cls, sig), static_cast<uint32_t>(g.nodes_.size()));
        if (inserted) {
            g.nodes_.push_back(CallNode{node, MethodId{}, /*resolved=*/false, {}, {}});
            g.resolved_refs_.emplace_back();
            node_class.push_back(cls);
            node_sig.push_back(sig);
        }
        return node;
    };

    // Per-DEX pool index -> id, filled on first use from the prepared strings.
    // Out-of-range indices (malformed input) resolve to the empty string, as
    // the impl's accessors do, and are simply not memoized.
    struct DexIds
    {
        std::vector<uint32_t> type, string, proto, method;
        std::unordered_map<uint32_t, uint32_t> call_site;
    };
    std::vector<DexIds> dex_ids(dex_count);
    std::size_t method_id_total = 0;
    for (std::size_t i = 0; i < dex_count; ++i) {
        const auto &dex = impl->dex_files()[i];
        dex_ids[i].type.assign(dex.type_ids().size(), kNone);
        dex_ids[i].string.assign(dex.string_ids().size(), kNone);
        dex_ids[i].proto.assign(dex.proto_ids().size(), kNone);
        dex_ids[i].method.assign(dex.method_ids().size(), kNone);
        method_id_total += dex.method_ids().size();
    }
    // Every node but the synthetic call_site ones comes from some method_id.
    g.nodes_.reserve(method_id_total);

    auto class_id = [&](std::size_t dex, uint32_t type_idx) -> uint32_t {
        auto &table = dex_ids[dex].type;
        if (type_idx >= table.size())
            return classes.id({});
        if (table[type_idx] == kNone)
            table[type_idx] =
                classes.id(prep[dex].type_view[type_idx], prep[dex].type_hash[type_idx]);
        return table[type_idx];
    };
    auto proto_id = [&](std::size_t dex, uint32_t proto_idx) -> uint32_t {
        auto &table = dex_ids[dex].proto;
        if (proto_idx >= table.size())
            return protos.id({});
        if (table[proto_idx] == kNone)
            table[proto_idx] =
                protos.id(prep[dex].proto[proto_idx], prep[dex].proto_hash[proto_idx]);
        return table[proto_idx];
    };
    // The signature of method_ids[method_idx], which must be in range.
    auto method_sig = [&](std::size_t dex, uint32_t method_idx) -> uint32_t {
        const auto &mid = impl->dex_files()[dex].method_ids()[method_idx];
        auto &names_of = dex_ids[dex].string;
        uint32_t name;
        if (mid.name_idx >= names_of.size()) {
            name = names.id({});
        }
        else {
            if (names_of[mid.name_idx] == kNone)
                names_of[mid.name_idx] = names.id(prep[dex].method_name[method_idx],
                                                  prep[dex].method_name_hash[method_idx]);
            name = names_of[mid.name_idx];
        }
        return sig_of(name, proto_id(dex, mid.proto_idx));
    };
    // The node method_id_of(dex, method_idx) names; out of range, that is the
    // empty MethodId.
    auto method_node = [&](std::size_t dex, uint32_t method_idx) -> uint32_t {
        auto &table = dex_ids[dex].method;
        if (method_idx >= table.size())
            return node_of(classes.id({}), sig_of(names.id({}), protos.id({})));
        if (table[method_idx] == kNone) {
            const auto &mid = impl->dex_files()[dex].method_ids()[method_idx];
            table[method_idx] = node_of(class_id(dex, mid.class_idx), method_sig(dex, method_idx));
        }
        return table[method_idx];
    };
    // invoke-custom targets get a synthetic node per (dex, call site).  The
    // dex index is part of the name — call_site_index is per-DEX; without it,
    // two DEX files' call_site #0 would collapse into one node.
    auto call_site_node = [&](std::size_t dex, uint32_t call_site_idx) {
        auto [it, inserted] = dex_ids[dex].call_site.try_emplace(call_site_idx, kNone);
        if (inserted) {
            owned.push_back("#" + std::to_string(dex) + ":" + std::to_string(call_site_idx));
            it->second =
                node_of(classes.id("call_site"), sig_of(names.id(owned.back()), protos.id({})));
        }
        return it->second;
    };

    // A. Register every defined method as a resolved node.  A defined method is
    // keyed by its class_def's class, which in a well-formed DEX is also its
    // method_id's class.
    struct Defined
    {
        uint32_t node_idx;
        std::size_t dex_idx;
        const DexPrep::Method *method;
    };
    std::vector<Defined> defined;
    std::size_t site_total = 0;
    for (std::size_t i = 0; i < dex_count; ++i) {
        for (const auto &m : prep[i].methods) {
            uint32_t node_idx = node_of(class_id(i, m.class_idx), method_sig(i, m.method_idx));
            g.nodes_[node_idx].resolved = true;
            // Last-loaded wins on duplicate definitions.
            g.resolved_refs_[node_idx] =
                CallGraph::ResolvedRef{i, m.method_idx, m.access_flags, m.code_off};
            if (m.code_off != 0)
                defined.push_back({node_idx, i, &m});
        }
        site_total += prep[i].sites.size();
    }

    // B. One Declared edge per call site.  The sites were found by
    // find_invokes(), which walks the code exactly like the decoder but decodes
    // nothing else, so building the graph does not fill the context's
    // instruction cache.
    g.edges_.reserve(site_total);
    for (const auto &d : defined) {
        const auto &sites = prep[d.dex_idx].sites;
        for (std::size_t k = d.method->sites_begin; k < d.method->sites_end; ++k) {
            const auto &s = sites[k];
            InvokeKind kind = *invoke_kind_from_opcode(s.opcode);
            uint32_t callee = kind == InvokeKind::Custom ? call_site_node(d.dex_idx, s.index)
                                                         : method_node(d.dex_idx, s.index);
            g.edges_.push_back(CallEdge{d.node_idx, callee, kind, s.offset});
        }
    }

    // No node is created after this point.
    const std::size_t node_count = g.nodes_.size();
    const std::size_t sig_count = sig_parts.size();

    // Work over [0, count) split into fixed-size chunks run on several threads;
    // fn(begin, end) handles one chunk.
    auto for_each_chunk = [&](std::size_t count, auto &&fn) {
        constexpr std::size_t kChunk = 16384;
        std::vector<std::size_t> chunks((count + kChunk - 1) / kChunk);
        for (std::size_t c = 0; c < chunks.size(); ++c)
            chunks[c] = c;
        for_each_parallel(chunks, std::min(threads, chunks.size()), [&](std::size_t c) {
            fn(c * kChunk, std::min(count, (c + 1) * kChunk));
        });
    };

    // ---- Node ids -------------------------------------------------------------
    // Every node's MethodId, built from the interned strings.  Each node is
    // independent, so the string copies are spread over the threads.
    for_each_chunk(node_count, [&](std::size_t begin, std::size_t end) {
        for (std::size_t n = begin; n < end; ++n) {
            auto [name, proto] = sig_parts[node_sig[n]];
            g.nodes_[n].id =
                MethodId{std::string(classes.view(node_class[n])), std::string(names.view(name)),
                         std::string(protos.view(proto))};
        }
    });

    // ---- The class hierarchy as integer arrays ----------------------------
    // Superclass, direct interfaces and children per class id, read from the
    // class_defs by the same rules build_class_hierarchy() uses for
    // ClassHierarchy (keep the two in sync): for a class defined more than
    // once, its superclass is the last one a definition names and its
    // interfaces are every definition's, in order; an interface list that
    // fails to parse contributes none.  Children are subclasses and
    // implementers, in no particular order — only the set of descendants
    // matters.
    std::vector<uint32_t> super_by_class;
    std::vector<std::pair<uint32_t, uint32_t>> class_iface; // (class, interface), in order
    for (std::size_t i = 0; i < dex_count; ++i) {
        const auto class_defs = impl->dex_files()[i].class_defs();
        for (std::size_t j = 0; j < class_defs.size(); ++j) {
            const auto &cdef = class_defs[j];
            uint32_t cls = class_id(i, cdef.class_idx);
            if (cdef.superclass_idx != kNone) {
                uint32_t super = class_id(i, cdef.superclass_idx);
                if (super_by_class.size() <= cls)
                    super_by_class.resize(cls + 1, kNone);
                super_by_class[cls] = super;
            }
            for (std::size_t k = prep[i].iface_offsets[j]; k < prep[i].iface_offsets[j + 1]; ++k)
                class_iface.emplace_back(cls, class_id(i, prep[i].iface_types[k]));
        }
    }
    const std::size_t class_count = classes.size();
    std::vector<uint32_t> super_of(class_count, kNone);
    std::copy(super_by_class.begin(), super_by_class.end(), super_of.begin());
    Rows ifaces, children;
    {
        std::vector<uint32_t> pairs(class_iface.size());
        for (uint32_t k = 0; k < pairs.size(); ++k)
            pairs[k] = k;
        Rows by_class =
            group_by(class_count, pairs, [&](uint32_t k) { return class_iface[k].first; });
        ifaces.offsets = std::move(by_class.offsets);
        ifaces.values.reserve(pairs.size());
        for (uint32_t k : by_class.values)
            ifaces.values.push_back(class_iface[k].second);

        // children: (parent, child) for every superclass and interface link.
        std::vector<std::pair<uint32_t, uint32_t>> links;
        for (uint32_t c = 0; c < class_count; ++c)
            if (super_of[c] != kNone)
                links.emplace_back(super_of[c], c);
        for (const auto &[cls, iface] : class_iface)
            links.emplace_back(iface, cls);
        std::vector<uint32_t> link_ids(links.size());
        for (uint32_t k = 0; k < link_ids.size(); ++k)
            link_ids[k] = k;
        Rows by_parent =
            group_by(class_count, link_ids, [&](uint32_t k) { return links[k].first; });
        children.offsets = std::move(by_parent.offsets);
        children.values.reserve(link_ids.size());
        for (uint32_t k : by_parent.values)
            children.values.push_back(links[k].second);
    }
    std::vector<bool> used_as_iface(class_count, false);
    for (uint32_t c : ifaces.values)
        used_as_iface[c] = true;

    // The transitive superinterfaces a class's lookup searches, in order: the
    // deduped interfaces seeded from every class of its superclass chain, then
    // breadth-first — JVMS 5.4.3.3 searches the superinterfaces of C, which
    // includes interfaces implemented anywhere up the chain.  Both walks are
    // cycle-guarded (a malformed DEX can encode `A extends B extends A`; an
    // unguarded walk is a hang).  Built once per class into a shared pool.
    std::vector<std::size_t> iface_begin(class_count, kNone), iface_end(class_count, 0);
    std::vector<uint32_t> iface_pool;
    std::vector<uint32_t> seen(class_count, 0);
    uint32_t seen_epoch = 0;
    std::vector<uint32_t> chain;
    auto superinterfaces = [&](uint32_t cls) -> std::span<const uint32_t> {
        if (iface_begin[cls] == kNone) {
            ++seen_epoch;
            chain.clear();
            for (uint32_t c = cls; c != kNone && seen[c] != seen_epoch; c = super_of[c]) {
                seen[c] = seen_epoch;
                chain.push_back(c);
            }
            ++seen_epoch;
            iface_begin[cls] = iface_pool.size();
            auto add_interfaces_of = [&](uint32_t c) {
                for (uint32_t i : row(ifaces.offsets, ifaces.values, c))
                    if (seen[i] != seen_epoch) {
                        seen[i] = seen_epoch;
                        iface_pool.push_back(i);
                    }
            };
            for (uint32_t c : chain)
                add_interfaces_of(c);
            for (std::size_t q = iface_begin[cls]; q < iface_pool.size(); ++q)
                add_interfaces_of(iface_pool[q]);
            iface_end[cls] = iface_pool.size();
        }
        return std::span(iface_pool).subspan(iface_begin[cls], iface_end[cls] - iface_begin[cls]);
    };

    // ---- Resolution, one signature at a time --------------------------------
    // Every question B2 and B3 ask is about a single signature: "what does a
    // lookup of sig from class X find?".  They are answered grouped by
    // signature.  For the current signature, the classes defining it are marked
    // in an array, and the answers are memoized per class in arrays — no
    // hashing or allocation per lookup.  An entry belongs to the current
    // signature iff its stamp equals sig_epoch.
    Rows nodes_by_sig;
    {
        std::vector<uint32_t> all(node_count);
        for (uint32_t n = 0; n < node_count; ++n)
            all[n] = n;
        nodes_by_sig = group_by(sig_count, all, [&](uint32_t n) { return node_sig[n]; });
    }
    auto flags_of = [&](uint32_t n) {
        return static_cast<AccessFlags>(g.resolved_refs_[n]->access_flags);
    };
    // The definitions a non-static lookup may stop at (see match_at).
    auto eligible = [&](uint32_t n) {
        if (!g.resolved_refs_[n].has_value())
            return false;
        auto flags = flags_of(n);
        return !has_flag(flags, AccessFlags::Private) &&
               !has_flag(flags, AccessFlags::Constructor) && !has_flag(flags, AccessFlags::Static);
    };

    uint32_t sig_epoch = 0;
    std::vector<uint32_t> def_at(class_count), def_stamp(class_count, 0);
    std::size_t sig_eligible = 0;  // eligible definitions of the current sig
    bool sig_in_interface = false; // one of them is on a class used as an interface
    auto begin_sig = [&](uint32_t sig) {
        ++sig_epoch;
        sig_eligible = 0;
        sig_in_interface = false;
        for (uint32_t n : row(nodes_by_sig.offsets, nodes_by_sig.values, sig)) {
            if (!g.resolved_refs_[n].has_value())
                continue;
            def_at[node_class[n]] = n;
            def_stamp[node_class[n]] = sig_epoch;
            if (eligible(n)) {
                ++sig_eligible;
                sig_in_interface = sig_in_interface || used_as_iface[node_class[n]];
            }
        }
    };

    // The current sig's definition in `cls` that a lookup may stop at, or
    // kNone.  Resolved-only is essential: unresolved nodes exist for every
    // referenced method_id (e.g. a stray `invoke-virtual Mid.foo`), and
    // stopping at one would recreate the dangling-edge problem this resolution
    // exists to fix.
    auto match_at = [&](uint32_t cls, bool static_walk) -> uint32_t {
        if (def_stamp[cls] != sig_epoch)
            return kNone;
        uint32_t n = def_at[cls];
        auto flags = flags_of(n);
        if (has_flag(flags, AccessFlags::Private) || has_flag(flags, AccessFlags::Constructor))
            return kNone;
        if (has_flag(flags, AccessFlags::Static) != static_walk)
            return kNone;
        return n;
    };
    auto is_abstract = [&](uint32_t n) { return has_flag(flags_of(n), AccessFlags::Abstract); };

    // First match along the superclass chain of `cls`, nearest first.  The
    // chain from a class is fixed, so the answer is memoized for every class
    // the walk passes; a walk that comes back to a class it already passed (a
    // malformed cycle) has found no match.
    std::vector<uint32_t> chain_memo[2], chain_stamp[2];
    for (int s = 0; s < 2; ++s) {
        chain_memo[s].assign(class_count, kNone);
        chain_stamp[s].assign(class_count, 0);
    }
    std::vector<uint32_t> walked(class_count, 0);
    uint32_t walk_epoch = 0;
    std::vector<uint32_t> path;
    auto chain_match = [&](uint32_t cls, bool static_walk) -> uint32_t {
        auto &memo_of = chain_memo[static_walk ? 1 : 0];
        auto &stamp_of = chain_stamp[static_walk ? 1 : 0];
        ++walk_epoch;
        path.clear();
        uint32_t result = kNone;
        for (uint32_t c = cls; c != kNone; c = super_of[c]) {
            if (stamp_of[c] == sig_epoch) {
                result = memo_of[c];
                break;
            }
            if (walked[c] == walk_epoch)
                break; // cycle
            walked[c] = walk_epoch;
            path.push_back(c);
            uint32_t m = match_at(c, static_walk);
            if (m != kNone) {
                result = m;
                break;
            }
        }
        for (uint32_t c : path) {
            memo_of[c] = result;
            stamp_of[c] = sig_epoch;
        }
        return result;
    };

    // Nearest definition of the current sig visible from `cls` (a non-static
    // lookup).  Priority: concrete chain match > interface default method >
    // abstract chain match > abstract interface declaration.  The chain scan
    // stops at its first match either way — an abstract redeclaration shadows
    // concrete ancestors.
    std::vector<uint32_t> resolve_memo(class_count), resolve_stamp(class_count, 0);
    auto resolve = [&](uint32_t cls) -> uint32_t {
        if (resolve_stamp[cls] == sig_epoch)
            return resolve_memo[cls];
        uint32_t first = chain_match(cls, false);
        uint32_t result = first;
        if (first == kNone || is_abstract(first)) {
            uint32_t found_default = kNone;
            uint32_t iface_abstract = kNone;
            if (sig_in_interface) {
                for (uint32_t i : superinterfaces(cls)) {
                    uint32_t m = match_at(i, false);
                    if (m == kNone)
                        continue;
                    if (!is_abstract(m)) { // default method
                        found_default = m;
                        break;
                    }
                    if (iface_abstract == kNone)
                        iface_abstract = m;
                }
            }
            result = found_default != kNone ? found_default
                     : first != kNone       ? first
                                            : iface_abstract;
        }
        resolve_memo[cls] = result;
        resolve_stamp[cls] = sig_epoch;
        return result;
    };

    // Descendants of a class (not including itself, unless a malformed cycle
    // leads back to it), breadth-first over the children rows.
    std::vector<uint32_t> visited(class_count, 0);
    uint32_t visit_epoch = 0;
    std::vector<uint32_t> queue;
    auto for_each_descendant = [&](uint32_t cls, auto &&fn) {
        ++visit_epoch;
        queue.clear();
        auto enqueue_children_of = [&](uint32_t c) {
            for (uint32_t child : row(children.offsets, children.values, c))
                if (visited[child] != visit_epoch) {
                    visited[child] = visit_epoch;
                    queue.push_back(child);
                }
        };
        enqueue_children_of(cls);
        for (std::size_t q = 0; q < queue.size(); ++q) {
            fn(queue[q]);
            enqueue_children_of(queue[q]);
        }
    };

    // The work, grouped by signature: B2 needs the dispatch targets of every
    // node an invoke-virtual / invoke-interface site declares; B3 needs the
    // upward resolution of every unresolved node a virtual / interface / super
    // site declares (non-static lookup) or a static site declares (static
    // lookup, superclass chain only — static interface methods are never
    // inherited; ART throws ICCE).
    std::vector<bool> dispatched(node_count, false);
    std::vector<uint8_t> lookups(node_count, 0); // bit 0: non-static, bit 1: static
    for (const auto &e : g.edges_) {
        if (dispatches(e))
            dispatched[e.callee_node] = true;
        if (g.nodes_[e.callee_node].resolved)
            continue;
        if (e.kind == InvokeKind::Virtual || e.kind == InvokeKind::Interface ||
            e.kind == InvokeKind::Super)
            lookups[e.callee_node] |= 1;
        else if (e.kind == InvokeKind::Static)
            lookups[e.callee_node] |= 2;
    }
    std::vector<uint32_t> work;
    for (uint32_t n = 0; n < node_count; ++n)
        if (dispatched[n] || lookups[n] != 0)
            work.push_back(n);
    Rows work_by_sig = group_by(sig_count, work, [&](uint32_t n) { return node_sig[n]; });

    std::vector<uint32_t> inherited[2]; // per node: non-static / static upward resolution
    inherited[0].assign(node_count, kNone);
    inherited[1].assign(node_count, kNone);
    std::vector<std::pair<std::size_t, std::size_t>> target_range(node_count, {0, 0});
    std::vector<uint32_t> target_pool;
    std::vector<uint32_t> targets;
    for (uint32_t sig = 0; sig < sig_count; ++sig) {
        auto nodes = row(work_by_sig.offsets, work_by_sig.values, sig);
        if (nodes.empty())
            continue;
        begin_sig(sig);
        for (uint32_t n : nodes) {
            uint32_t cls = node_class[n];
            if (lookups[n] & 1)
                inherited[0][n] = resolve(cls);
            if (lookups[n] & 2)
                inherited[1][n] = chain_match(cls, true);
            if (!dispatched[n])
                continue;

            // B2. Class-Hierarchy Analysis: one dispatch target per distinct
            // definition a loaded descendant of the declared class resolves to.
            // A descendant defining the method contributes its own definition
            // (the lookup is self-inclusive); one that doesn't contributes what
            // it inherits — this pulls in implementations from outside the
            // declared class's subtree (`class C extends Base implements I`
            // dispatching I.m to Base.m).  The declared node's own resolution is
            // excluded: every site already reaches it via its Declared or
            // InheritedResolution edge.  Every possible target is an eligible
            // definition of sig; when the only ones are the node itself and its
            // own resolution, the descendant walk is skipped (`getClass()`
            // would otherwise visit every class).
            uint32_t own = resolve(cls);
            std::size_t excluded = (eligible(n) ? 1 : 0) + (own != kNone && own != n ? 1 : 0);
            if (sig_eligible == excluded)
                continue;
            targets.clear();
            for_each_descendant(cls, [&](uint32_t desc) {
                uint32_t t = resolve(desc);
                if (t != kNone && t != n && t != own)
                    targets.push_back(t);
            });
            sort_unique(targets);
            target_range[n] = {target_pool.size(), targets.size()};
            target_pool.insert(target_pool.end(), targets.begin(), targets.end());
        }
    }

    // The dispatch targets as compressed rows in node order, and their reverse
    // (dispatch_sources) by counting sort.  Both are sorted by node index: each
    // target row was sorted above, and sources are added in node order.
    g.dispatch_target_offsets_.assign(node_count + 1, 0);
    g.dispatch_target_values_.reserve(target_pool.size());
    for (uint32_t n = 0; n < node_count; ++n) {
        g.dispatch_target_offsets_[n] = g.dispatch_target_values_.size();
        auto [begin, size] = target_range[n];
        g.dispatch_target_values_.insert(g.dispatch_target_values_.end(),
                                         target_pool.begin() + begin,
                                         target_pool.begin() + begin + size);
    }
    g.dispatch_target_offsets_[node_count] = g.dispatch_target_values_.size();

    g.dispatch_source_offsets_.assign(node_count + 1, 0);
    for (uint32_t t : g.dispatch_target_values_)
        ++g.dispatch_source_offsets_[static_cast<std::size_t>(t) + 1];
    for (std::size_t i = 0; i < node_count; ++i)
        g.dispatch_source_offsets_[i + 1] += g.dispatch_source_offsets_[i];
    g.dispatch_source_values_.resize(g.dispatch_target_values_.size());
    std::vector<std::size_t> next_free(g.dispatch_source_offsets_.begin(),
                                       g.dispatch_source_offsets_.end() - 1);
    for (uint32_t n = 0; n < node_count; ++n)
        for (uint32_t t : g.dispatch_targets(n))
            g.dispatch_source_values_[next_free[t]++] = n;

    // B3. Upward resolution: a declared target no loaded class defines gets one
    // InheritedResolution edge to what Dalvik resolution would find, for
    // virtual, interface, super and static sites.  The lookups ran above; only
    // Declared edges are scanned — dispatch targets and resolutions belong to
    // the declared node, and computed for a resolution target instead they
    // would pull in overrides from sibling branches of the declared class.
    std::vector<CallEdge> inherited_edges;
    for (const auto &e : g.edges_) {
        if (e.origin != EdgeOrigin::Declared)
            continue;
        if (e.kind != InvokeKind::Virtual && e.kind != InvokeKind::Interface &&
            e.kind != InvokeKind::Super && e.kind != InvokeKind::Static)
            continue;
        if (g.nodes_[e.callee_node].resolved)
            continue;
        uint32_t t = inherited[e.kind == InvokeKind::Static ? 1 : 0][e.callee_node];
        if (t != kNone)
            inherited_edges.push_back(
                CallEdge{e.caller_node, t, e.kind, e.code_offset, EdgeOrigin::InheritedResolution});
    }
    g.edges_.insert(g.edges_.end(), inherited_edges.begin(), inherited_edges.end());

    // C. Node out/in lists: edge indices bucketed per node by a counting sort
    // (which keeps them in edge order), then copied into each node's vectors on
    // several threads.
    {
        std::vector<uint32_t> edge_ids(g.edges_.size());
        for (uint32_t e = 0; e < edge_ids.size(); ++e)
            edge_ids[e] = e;
        Rows out_rows =
            group_by(node_count, edge_ids, [&](uint32_t e) { return g.edges_[e].caller_node; });
        Rows in_rows =
            group_by(node_count, edge_ids, [&](uint32_t e) { return g.edges_[e].callee_node; });
        for_each_chunk(node_count, [&](std::size_t begin, std::size_t end) {
            for (std::size_t n = begin; n < end; ++n) {
                auto out = row(out_rows.offsets, out_rows.values, static_cast<uint32_t>(n));
                auto in = row(in_rows.offsets, in_rows.values, static_cast<uint32_t>(n));
                g.nodes_[n].outgoing.assign(out.begin(), out.end());
                g.nodes_[n].incoming.assign(in.begin(), in.end());
            }
        });
    }

    // find()'s table, at most half full.  Node ids are distinct, so each one
    // just takes the first free slot of its probe sequence.  The hashing is
    // spread over the threads; the inserts are not.
    std::vector<std::size_t> id_hash(node_count);
    for_each_chunk(node_count, [&](std::size_t begin, std::size_t end) {
        for (std::size_t n = begin; n < end; ++n)
            id_hash[n] = MethodIdHash{}(g.nodes_[n].id);
    });
    std::size_t index_size = 64;
    while (index_size < node_count * 2)
        index_size *= 2;
    g.index_.assign(index_size, 0);
    for (uint32_t n = 0; n < node_count; ++n) {
        std::size_t i = id_hash[n] & (index_size - 1);
        while (g.index_[i] != 0)
            i = (i + 1) & (index_size - 1);
        g.index_[i] = n + 1;
    }

    return g;
}

} // namespace dex
