#include "xrefs.hpp"

#include "xrefs_builder.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "analysis_context_impl.hpp"
#include "code_scan.hpp"
#include "joining_threads.hpp"
#include "method.hpp"
#include "opcode_kinds.hpp"
#include "raw/parser.hpp"

namespace dex {

std::optional<TypeUseKind> detail::type_use_kind_from_opcode(uint8_t opcode)
{
    switch (opcode) {
    case 0x1c:
        return TypeUseKind::ConstClass;
    case 0x1f:
        return TypeUseKind::CheckCast;
    case 0x20:
        return TypeUseKind::InstanceOf;
    case 0x22:
        return TypeUseKind::NewInstance;
    case 0x23:
        return TypeUseKind::NewArray;
    case 0x24:
    case 0x25:
        return TypeUseKind::FilledNewArray;
    default:
        return std::nullopt;
    }
}

namespace {

// The sites a Range covers, or none for a missing key.
template <typename Map, typename Key, typename Xref>
std::span<const Xref> sites_of(const Map &map, const Key &key, const std::vector<Xref> &sites)
{
    auto it = map.find(key);
    if (it == map.end())
        return {};
    return std::span<const Xref>(sites).subspan(it->second.begin, it->second.count);
}

} // namespace

std::optional<Method> Xrefs::referrer_method(uint32_t referrer) const
{
    if (referrer >= referrer_refs_.size())
        return std::nullopt;

    // The index lives inside the context it was built from, so impl_ is alive
    // whenever the index is.
    const auto &ref = referrer_refs_[referrer];
    return Method(impl_->shared_from_this(), ref.dex_idx, ref.method_abs_idx, ref.access_flags,
                  ref.code_off);
}

std::span<const MethodXref> Xrefs::method_refs(const MethodId &id) const
{
    return sites_of(method_map_, id, method_sites_);
}

std::vector<std::pair<const MethodId *, std::span<const MethodXref>>>
Xrefs::method_refs_by_name(std::string_view name) const
{
    std::vector<std::pair<const MethodId *, std::span<const MethodXref>>> out;
    auto it = name_index_.find(name);
    if (it == name_index_.end())
        return out;
    out.reserve(it->second.size());
    for (const auto *entry : it->second)
        out.emplace_back(&entry->first, std::span<const MethodXref>(method_sites_)
                                            .subspan(entry->second.begin, entry->second.count));
    return out;
}

std::span<const FieldXref> Xrefs::field_refs(const FieldId &id) const
{
    return sites_of(field_map_, id, field_sites_);
}

std::vector<FieldXref> Xrefs::field_reads(const FieldId &id) const
{
    std::vector<FieldXref> out;
    for (const auto &x : field_refs(id))
        if (!x.is_write)
            out.push_back(x);
    return out;
}

std::vector<FieldXref> Xrefs::field_writes(const FieldId &id) const
{
    std::vector<FieldXref> out;
    for (const auto &x : field_refs(id))
        if (x.is_write)
            out.push_back(x);
    return out;
}

std::span<const StringXref> Xrefs::string_refs(std::string_view value) const
{
    return sites_of(string_map_, value, string_sites_);
}

std::vector<std::string_view> Xrefs::referenced_strings() const
{
    std::vector<std::string_view> out;
    out.reserve(string_map_.size());
    for (const auto &[value, sites] : string_map_)
        out.emplace_back(value);
    return out;
}

std::span<const TypeXref> Xrefs::type_refs(std::string_view descriptor) const
{
    return sites_of(type_map_, descriptor, type_sites_);
}

namespace detail {

/** Lets build_xrefs() fill the index's private members. */
struct XrefsAccess
{
    static Xrefs build(const AnalysisContextImpl &impl);
};

Xrefs build_xrefs(const AnalysisContextImpl &impl) { return XrefsAccess::build(impl); }

} // namespace detail

namespace {

constexpr uint32_t kNoKey = UINT32_MAX;

bool is_string_opcode(uint8_t op) { return op == 0x1a || op == 0x1b; }
bool is_field_opcode(uint8_t op) { return op >= 0x52 && op <= 0x6d; }
bool is_invoke_opcode(uint8_t op)
{
    return (op >= 0x6e && op <= 0x72) || (op >= 0x74 && op <= 0x78) || op == 0xfa || op == 0xfb;
}

// The names one DEX file's pool indices stand for, decoded straight from the
// file and not through the context's string cache: the index keeps its own
// copy of every key, so caching them as well would hold each string twice for
// the context's lifetime.  Results are the same as string_of(),
// type_descriptor_of(), proto_descriptor_of(), field_id_of() and
// method_id_of(), including the empty result for an index out of range.
// Type descriptors and prototypes repeat a lot, so they are memoized; the
// memos die with the object.
class DexNames
{
  public:
    DexNames(const detail::AnalysisContextImpl &impl, std::size_t dex_idx)
        : impl_(impl), dex_idx_(dex_idx), dex_(impl.dex_files()[dex_idx]),
          buf_(impl.buffer_of(dex_idx)), types_(dex_.type_ids().size()),
          protos_(dex_.proto_ids().size())
    {
    }

    std::string string(uint32_t idx) const
    {
        const auto string_ids = dex_.string_ids();
        if (idx >= string_ids.size())
            return {};
        auto r = raw::parser::parse_string(buf_, string_ids[idx]);
        return r.has_value() ? std::move(r->first) : std::string{};
    }

    const std::string &type(uint32_t idx)
    {
        static const std::string kEmpty;
        if (idx >= types_.size())
            return kEmpty;
        if (!types_[idx].has_value())
            types_[idx] = string(dex_.type_ids()[idx]);
        return *types_[idx];
    }

    // The same "(params)ret" descriptor as build_proto_descriptor().
    const std::string &proto(uint32_t idx)
    {
        static const std::string kEmpty;
        if (idx >= protos_.size())
            return kEmpty;
        if (protos_[idx].has_value())
            return *protos_[idx];
        const raw::ProtoIdItem &proto = dex_.proto_ids()[idx];
        std::string out = "(";
        if (proto.parameters_off != 0) {
            auto type_list = raw::parser::parse_type_list(buf_, proto.parameters_off);
            if (type_list.has_value()) {
                for (auto type_idx : type_list->type_ids)
                    out += type(type_idx);
            }
            else {
                out += "<unparseable>";
            }
        }
        out += ')';
        out += type(proto.return_type_idx);
        protos_[idx] = std::move(out);
        return *protos_[idx];
    }

    FieldId field(uint32_t idx)
    {
        const auto *item = impl_.field_item(dex_idx_, idx);
        if (!item)
            return FieldId{};
        return FieldId{type(item->class_idx), string(item->name_idx), type(item->type_idx)};
    }

    MethodId method(uint32_t idx)
    {
        const auto *item = impl_.method_item(dex_idx_, idx);
        if (!item)
            return MethodId{};
        return MethodId{type(item->class_idx), string(item->name_idx), proto(item->proto_idx)};
    }

  private:
    const detail::AnalysisContextImpl &impl_;
    std::size_t dex_idx_;
    const raw::DexFile &dex_;
    std::span<const uint8_t> buf_;
    std::vector<std::optional<std::string>> types_;
    std::vector<std::optional<std::string>> protos_;
};

// The sites of one kind (strings, types, fields or methods) in one DEX file.
// `keys` holds the distinct values the sites name, in order of first use;
// site k names keys[site_keys[k]].  A site's `referrer` is still the DEX
// file's own referrer number.
template <typename Key, typename Xref> struct KindScan
{
    std::vector<Key> keys;
    std::vector<Xref> sites;
    std::vector<uint32_t> site_keys;
};

// What one DEX file contributes to the index, found without touching any other
// DEX file's state, so the DEX files can be scanned on several threads.
struct DexScan
{
    // A method with at least one indexed instruction, in discovery order:
    // class_defs order (first definitions only), direct then virtual methods.
    struct Referrer
    {
        uint32_t method_idx;
        uint32_t access_flags;
        uint32_t code_off;
        MethodId id;
    };
    std::vector<Referrer> referrers;
    KindScan<std::string, StringXref> strings;
    KindScan<std::string, TypeXref> types;
    KindScan<FieldId, FieldXref> fields;
    KindScan<MethodId, MethodXref> methods;
};

// Record one site in `kind`, giving the pool index a key on its first use.
// `keys_of` has one slot per pool entry plus one, shared by every index out of
// range (they all stand for the same empty value).
template <typename Key, typename Xref, typename Resolve>
void add_site(KindScan<Key, Xref> &kind, std::vector<uint32_t> &keys_of, uint32_t idx,
              const Xref &site, Resolve &&resolve)
{
    uint32_t &key = keys_of[std::min<std::size_t>(idx, keys_of.size() - 1)];
    if (key == kNoKey) {
        key = static_cast<uint32_t>(kind.keys.size());
        kind.keys.push_back(resolve(idx));
    }
    kind.sites.push_back(site);
    kind.site_keys.push_back(key);
}

DexScan scan_dex(const detail::AnalysisContextImpl &impl, std::size_t dex_idx)
{
    const raw::DexFile &dex = impl.dex_files()[dex_idx];
    DexNames names(impl, dex_idx);
    DexScan scan;
    std::vector<uint32_t> string_keys(dex.string_ids().size() + 1, kNoKey);
    std::vector<uint32_t> type_keys(dex.type_ids().size() + 1, kNoKey);
    std::vector<uint32_t> field_keys(dex.field_ids().size() + 1, kNoKey);
    std::vector<uint32_t> method_keys(dex.method_ids().size() + 1, kNoKey);

    std::vector<detail::RefSite> found; // one method's sites; reused
    std::optional<raw::CodeItem> scratch;
    const auto class_defs = dex.class_defs();
    for (uint32_t j = 0; j < static_cast<uint32_t>(class_defs.size()); ++j) {
        // Only the definition a class loader would load: the code of a later
        // definition of the same class never runs.
        if (!impl.is_winning_definition(dex_idx, j))
            continue;
        const raw::ClassDataItem *cd = impl.get_class_data(dex_idx, class_defs[j].class_data_off);
        if (!cd)
            continue;
        for (const auto *methods : {&cd->direct_methods, &cd->virtual_methods})
            for (const raw::EncodedMethod &em : *methods) {
                // The code item is only borrowed: keeping every one alive, or
                // decoding every method into the instruction cache, for the
                // context's lifetime would cost more than the index itself.
                const raw::CodeItem *code = impl.peek_code_item(dex_idx, em.code_off, scratch);
                if (!code)
                    continue;
                found.clear();
                detail::find_refs(code->insns, found);
                if (found.empty())
                    continue;
                // Only methods with an indexed instruction become referrers.
                const auto referrer = static_cast<uint32_t>(scan.referrers.size());
                for (const detail::RefSite &site : found) {
                    const uint8_t op = site.opcode;
                    if (is_string_opcode(op)) {
                        add_site(scan.strings, string_keys, site.index,
                                 StringXref{referrer, site.offset},
                                 [&](uint32_t i) { return names.string(i); });
                    }
                    else if (is_field_opcode(op)) {
                        const bool is_static = op >= 0x60;
                        const bool is_write = op >= (is_static ? 0x67 : 0x59);
                        add_site(scan.fields, field_keys, site.index,
                                 FieldXref{referrer, site.offset, is_write, is_static},
                                 [&](uint32_t i) { return names.field(i); });
                    }
                    else if (is_invoke_opcode(op)) {
                        add_site(
                            scan.methods, method_keys, site.index,
                            MethodXref{referrer, site.offset, *detail::invoke_kind_from_opcode(op)},
                            [&](uint32_t i) { return names.method(i); });
                    }
                    else {
                        add_site(
                            scan.types, type_keys, site.index,
                            TypeXref{referrer, site.offset, *detail::type_use_kind_from_opcode(op)},
                            [&](uint32_t i) { return names.type(i); });
                    }
                }
                scan.referrers.push_back(DexScan::Referrer{
                    em.method_idx, em.access_flags, em.code_off, names.method(em.method_idx)});
            }
    }
    return scan;
}

// Build one kind's map and site array from every DEX file's scan of that kind,
// and free those scans.  Keys are inserted DEX file by DEX file, each in order
// of first use: the order the index has always inserted them in, so the map's
// iteration order (referenced_strings(), method_refs_by_name()) does not
// change.  Each key's sites stay in discovery order.
template <typename Map, typename Key, typename Xref>
void merge_kind(Map &map, std::vector<Xref> &out, std::vector<KindScan<Key, Xref> *> scans,
                const std::vector<uint32_t> &referrer_base)
{
    // Number the keys across DEX files.  While building, a key's Range holds
    // its number in `begin`.
    std::vector<typename Map::mapped_type *> ranges;
    std::vector<std::vector<uint32_t>> number_of(scans.size());
    for (std::size_t i = 0; i < scans.size(); ++i) {
        number_of[i].reserve(scans[i]->keys.size());
        for (Key &key : scans[i]->keys) {
            auto [it, inserted] = map.try_emplace(std::move(key));
            if (inserted) {
                it->second.begin = ranges.size();
                ranges.push_back(&it->second);
            }
            number_of[i].push_back(static_cast<uint32_t>(it->second.begin));
        }
        scans[i]->keys = {};
    }

    // Count each key's sites, then give each key its place in `out`.
    std::vector<std::size_t> next(ranges.size(), 0);
    for (std::size_t i = 0; i < scans.size(); ++i)
        for (uint32_t key : scans[i]->site_keys)
            ++next[number_of[i][key]];
    std::size_t total = 0;
    for (std::size_t n = 0; n < ranges.size(); ++n) {
        ranges[n]->begin = total;
        ranges[n]->count = next[n];
        next[n] = total;
        total += ranges[n]->count;
    }

    // Place the sites, DEX file by DEX file, so each key's sites stay in order.
    out.resize(total);
    for (std::size_t i = 0; i < scans.size(); ++i) {
        KindScan<Key, Xref> &scan = *scans[i];
        for (std::size_t k = 0; k < scan.sites.size(); ++k) {
            Xref site = scan.sites[k];
            site.referrer += referrer_base[i];
            out[next[number_of[i][scan.site_keys[k]]]++] = site;
        }
        scan = {};
    }
}

// Pointers to member `kind` of every scan.
template <typename Key, typename Xref>
std::vector<KindScan<Key, Xref> *> kind_of(std::vector<DexScan> &scans,
                                           KindScan<Key, Xref> DexScan::*kind)
{
    std::vector<KindScan<Key, Xref> *> out;
    out.reserve(scans.size());
    for (DexScan &scan : scans)
        out.push_back(&(scan.*kind));
    return out;
}

} // namespace

Xrefs detail::XrefsAccess::build(const AnalysisContextImpl &impl)
{
    Xrefs x;
    x.impl_ = &impl;

    // Each DEX file is scanned on its own thread.  Everything that numbers
    // things then runs in a fixed order — DEX files in load order, sites in
    // discovery order — so the index never depends on the thread count.
    const std::size_t dex_count = impl.dex_files().size();
    std::vector<DexScan> scans(dex_count);
    impl.for_each_dex_parallel([&](std::size_t i) { scans[i] = scan_dex(impl, i); });

    // The referrer numbers of DEX file i start after those of the files before it.
    std::vector<uint32_t> referrer_base(dex_count);
    std::size_t referrers = 0;
    std::size_t sites = 0;
    for (std::size_t i = 0; i < dex_count; ++i) {
        referrer_base[i] = static_cast<uint32_t>(referrers);
        referrers += scans[i].referrers.size();
        sites += scans[i].strings.sites.size() + scans[i].types.sites.size() +
                 scans[i].fields.sites.size() + scans[i].methods.sites.size();
    }

    // The four maps and the referrer list are independent, so each is built on
    // its own thread when there is enough work to pay for starting threads.
    auto build_part = [&](std::size_t part) {
        switch (part) {
        case 0:
            x.referrer_ids_.reserve(referrers);
            x.referrer_refs_.reserve(referrers);
            for (std::size_t i = 0; i < dex_count; ++i) {
                for (DexScan::Referrer &ref : scans[i].referrers) {
                    x.referrer_ids_.push_back(std::move(ref.id));
                    x.referrer_refs_.push_back(
                        Xrefs::ReferrerRef{i, ref.method_idx, ref.access_flags, ref.code_off});
                }
                scans[i].referrers = {};
            }
            break;
        case 1:
            merge_kind(x.string_map_, x.string_sites_, kind_of(scans, &DexScan::strings),
                       referrer_base);
            break;
        case 2:
            merge_kind(x.type_map_, x.type_sites_, kind_of(scans, &DexScan::types), referrer_base);
            break;
        case 3:
            merge_kind(x.field_map_, x.field_sites_, kind_of(scans, &DexScan::fields),
                       referrer_base);
            break;
        default:
            merge_kind(x.method_map_, x.method_sites_, kind_of(scans, &DexScan::methods),
                       referrer_base);
            // Secondary index: bare method name → map entries.  Built after
            // the map so every key's address is final.
            for (const auto &entry : x.method_map_)
                x.name_index_[entry.first.name].push_back(&entry);
            break;
        }
    };
    const std::vector<std::size_t> parts = {4, 3, 0, 1, 2}; // biggest first
    constexpr std::size_t kMinParallelSites = 1u << 16;
    const bool worth_it = impl.threads != 0 || sites >= kMinParallelSites;
    const std::size_t threads = worth_it ? std::min(impl.thread_budget(), parts.size()) : 1;
    for_each_parallel(parts, threads, build_part);
    return x;
}

} // namespace dex
