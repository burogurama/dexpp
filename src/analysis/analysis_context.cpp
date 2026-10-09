#include "analysis_context.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <exception>
#include <fstream>
#include <numeric>
#include <system_error>
#include <thread>
#include <unordered_set>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#define DEXPP_HAS_MMAP 1
#endif

#include "analysis_context_impl.hpp"
#include "raw/parser.hpp"
#include "raw/zip.hpp"

namespace dex {

using detail::AnalysisContextImpl;
using detail::ManagedBuffer;

namespace detail {

ManagedBuffer::ManagedBuffer(std::vector<uint8_t> heap) : heap_(std::move(heap))
{
    data_ = heap_.data();
    size_ = heap_.size();
}

ManagedBuffer::~ManagedBuffer()
{
#ifdef DEXPP_HAS_MMAP
    if (map_addr_ != nullptr)
        ::munmap(map_addr_, size_);
#endif
}

ManagedBuffer::ManagedBuffer(ManagedBuffer &&other) noexcept
    : heap_(std::move(other.heap_)), data_(other.data_), size_(other.size_),
      map_addr_(other.map_addr_)
{
    other.data_ = nullptr;
    other.size_ = 0;
    other.map_addr_ = nullptr;
}

ManagedBuffer &ManagedBuffer::operator=(ManagedBuffer &&other) noexcept
{
    if (this != &other) {
#ifdef DEXPP_HAS_MMAP
        if (map_addr_ != nullptr)
            ::munmap(map_addr_, size_);
#endif
        heap_ = std::move(other.heap_);
        data_ = other.data_;
        size_ = other.size_;
        map_addr_ = other.map_addr_;
        other.data_ = nullptr;
        other.size_ = 0;
        other.map_addr_ = nullptr;
    }
    return *this;
}

std::optional<ManagedBuffer> ManagedBuffer::map_file(const std::string &path)
{
#ifdef DEXPP_HAS_MMAP
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0)
        return std::nullopt;

    struct stat st;
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size == 0) {
        ::close(fd);
        return std::nullopt;
    }

    void *addr =
        ::mmap(nullptr, static_cast<std::size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd); // the mapping keeps its own reference
    if (addr == MAP_FAILED)
        return std::nullopt;

    ManagedBuffer buf;
    buf.data_ = static_cast<const uint8_t *>(addr);
    buf.size_ = static_cast<std::size_t>(st.st_size);
    buf.map_addr_ = addr;
    return buf;
#else
    (void)path;
    return std::nullopt;
#endif
}

} // namespace detail

AnalysisContext::AnalysisContext(std::shared_ptr<AnalysisContextImpl> impl) : impl_(std::move(impl))
{
}

static std::expected<std::vector<uint8_t>, AnalysisError> read_file(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return std::unexpected(
            AnalysisError{AnalysisError::Code::FileNotFound, "Cannot open file: " + path});
    }

    file.seekg(0, std::ios::end);
    auto size = static_cast<std::size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    file.read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(size));
    return buffer;
}

// The analysis layer never eagerly decodes the string pool; string_of()
// decodes and caches each string on first use.
constexpr raw::parser::ParseOptions kLazyParse{.decode_strings = false};

// Parse a DEX out of an already-owned buffer, moving it into a new slot on
// success.  @p context prefixes any parse-error message (path / entry name).
static std::expected<void, AnalysisError> add_slot(AnalysisContextImpl &impl, ManagedBuffer buffer,
                                                   const std::string &context)
{
    auto dex_result = raw::parser::parse_buffer(buffer.bytes(), kLazyParse);
    if (!dex_result.has_value()) {
        return std::unexpected(AnalysisError{AnalysisError::Code::InvalidDexFile,
                                             context + dex_result.error().message});
    }
    impl.dex_files_vec.push_back(std::move(dex_result.value()));
    impl.slots.push_back(std::make_unique<AnalysisContextImpl::DexSlot>(std::move(buffer)));
    return {};
}

// Load a standalone .dex from disk, preferring an mmap over a heap copy.
static std::expected<void, AnalysisError> load_dex_file(AnalysisContextImpl &impl,
                                                        const std::string &path)
{
    if (auto mapped = ManagedBuffer::map_file(path); mapped.has_value())
        return add_slot(impl, std::move(*mapped), path + ": ");

    auto buffer = read_file(path);
    if (!buffer.has_value())
        return std::unexpected(buffer.error());
    return add_slot(impl, ManagedBuffer(std::move(*buffer)), path + ": ");
}

/** Multidex slot number of a ZIP entry name, or nullopt if the entry is not a
 *  top-level classes*.dex: "classes.dex" -> 1, "classesN.dex" -> N (N >= 2). */
static std::optional<uint32_t> multidex_index(std::string_view name)
{
    if (name == "classes.dex")
        return 1;
    if (!name.starts_with("classes") || !name.ends_with(".dex"))
        return std::nullopt;
    std::string_view digits = name.substr(7, name.size() - 7 - 4);
    uint32_t n = 0;
    auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), n);
    if (ec != std::errc{} || ptr != digits.data() + digits.size() || n < 2)
        return std::nullopt;
    return n;
}

std::expected<AnalysisContext, AnalysisError> AnalysisContext::from_dex(const std::string &path)
{
    auto impl = std::make_shared<AnalysisContextImpl>();
    if (auto r = load_dex_file(*impl, path); !r.has_value())
        return std::unexpected(r.error());
    return AnalysisContext(std::move(impl));
}

std::expected<AnalysisContext, AnalysisError>
AnalysisContext::from_dex_files(const std::vector<std::string> &paths)
{
    auto impl = std::make_shared<AnalysisContextImpl>();
    for (const auto &path : paths) {
        if (auto r = load_dex_file(*impl, path); !r.has_value())
            return std::unexpected(r.error());
    }
    return AnalysisContext(std::move(impl));
}

// One DEX entry of an APK, decompressed and parsed, ready to become a slot.
struct LoadedDex
{
    std::vector<uint8_t> bytes;
    raw::DexFile dex;
};

// Decompress and parse one DEX entry.  Touches nothing but its arguments, so
// it is safe to run on several threads at once.
static std::expected<LoadedDex, AnalysisError> load_apk_entry(std::span<const uint8_t> apk_bytes,
                                                              const raw::zip::Entry &entry,
                                                              const std::string &label)
{
    auto buf = raw::zip::read_entry(apk_bytes, entry);
    if (!buf.has_value()) {
        return std::unexpected(
            AnalysisError{AnalysisError::Code::InvalidApk,
                          label + ": " + entry.name + ": " + buf.error().message});
    }
    auto dex = raw::parser::parse_buffer(*buf, kLazyParse);
    if (!dex.has_value()) {
        return std::unexpected(
            AnalysisError{AnalysisError::Code::InvalidDexFile,
                          label + ": " + entry.name + ": " + dex.error().message});
    }
    return LoadedDex{std::move(*buf), std::move(*dex)};
}

static void add_loaded_slot(AnalysisContextImpl &impl, LoadedDex loaded)
{
    impl.dex_files_vec.push_back(std::move(loaded.dex));
    impl.slots.push_back(
        std::make_unique<AnalysisContextImpl::DexSlot>(ManagedBuffer(std::move(loaded.bytes))));
}

// How many threads should load @p entries.  Decompression is almost all of
// the load time, and measurements showed extra threads only pay off from 3
// deflated entries up: below that, starting a thread and touching its fresh
// output pages costs more than it saves.
static std::size_t apk_load_threads(unsigned requested,
                                    std::span<const raw::zip::Entry *const> entries)
{
    constexpr std::size_t kMinDeflatedForParallel = 3;
    auto deflated = static_cast<std::size_t>(std::count_if(
        entries.begin(), entries.end(), [](const raw::zip::Entry *e) { return e->method == 8; }));
    if (requested == 1 || deflated < kMinDeflatedForParallel)
        return 1;
    std::size_t threads =
        requested != 0 ? requested : std::max(1u, std::thread::hardware_concurrency());
    return std::min(threads, entries.size());
}

struct EntryResult
{
    /// Unset when the entry was skipped because an earlier entry failed.
    std::optional<std::expected<LoadedDex, AnalysisError>> dex;
    std::exception_ptr exception;
};

// Load every entry on @p threads threads (the calling thread included).
// results[i] belongs to entries[i].  Once entry i fails, entries after i are
// skipped — the serial loop would never reach them — but every entry before
// i is still loaded, so the caller can report the same first failure the
// serial loop would.
static std::vector<EntryResult>
load_apk_entries_parallel(std::span<const uint8_t> apk_bytes,
                          std::span<const raw::zip::Entry *const> entries, const std::string &label,
                          std::size_t threads)
{
    const std::size_t n = entries.size();
    std::vector<EntryResult> results(n);

    // Hand out the biggest entries first, so a large DEX picked up last does
    // not leave the other threads idle while it finishes.
    std::vector<std::size_t> order(n);
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return entries[a]->uncompressed_size > entries[b]->uncompressed_size;
    });

    std::atomic<std::size_t> next{0};
    std::atomic<std::size_t> first_failed{n}; // n = nothing failed yet
    auto record_failure = [&](std::size_t i) {
        std::size_t current = first_failed.load();
        while (i < current && !first_failed.compare_exchange_weak(current, i)) {
        }
    };
    auto work = [&] {
        for (std::size_t k = next.fetch_add(1); k < n; k = next.fetch_add(1)) {
            std::size_t i = order[k];
            if (i > first_failed.load())
                continue;
            // An exception escaping a thread calls std::terminate and kills
            // the whole process; keep it and rethrow it on the calling thread.
            try {
                results[i].dex = load_apk_entry(apk_bytes, *entries[i], label);
                if (!results[i].dex->has_value())
                    record_failure(i);
            }
            catch (...) {
                results[i].exception = std::current_exception();
                record_failure(i);
            }
        }
    };

    {
        std::vector<std::jthread> pool;
        try {
            pool.reserve(threads - 1);
            for (std::size_t t = 1; t < threads; ++t)
                pool.emplace_back(work);
        }
        catch (const std::exception &) {
            // Could not start every thread (resource limits): the threads
            // that did start, plus this one, still drain the whole queue.
        }
        work();
    } // the pool joins here
    return results;
}

std::expected<AnalysisContext, AnalysisError>
AnalysisContext::from_apk(const std::string &path, const ApkLoadOptions &options)
{
    // The APK is mapped (or read) once; from_apk_buffer decompresses each
    // classes*.dex out of it, then the archive mapping is released here.
    auto apk = ManagedBuffer::map_file(path);
    std::optional<std::vector<uint8_t>> apk_heap;
    if (!apk.has_value()) {
        auto buf = read_file(path);
        if (!buf.has_value())
            return std::unexpected(buf.error());
        apk_heap = std::move(*buf);
    }
    std::span<const uint8_t> apk_bytes = apk.has_value() ? apk->bytes() : std::span(*apk_heap);
    return from_apk_buffer(apk_bytes, path, options);
}

std::expected<AnalysisContext, AnalysisError>
AnalysisContext::from_apk_buffer(std::span<const uint8_t> apk_bytes, const std::string &label,
                                 const ApkLoadOptions &options)
{
    auto all_entries = raw::zip::entries(apk_bytes);
    if (!all_entries.has_value()) {
        return std::unexpected(AnalysisError{AnalysisError::Code::InvalidApk,
                                             label + ": " + all_entries.error().message});
    }

    // A name repeated in the central directory loads only its first entry,
    // matching what read_entry(name) and apk::Apk::read return for it.
    std::vector<std::pair<uint32_t, const raw::zip::Entry *>> dex_entries;
    std::unordered_set<std::string_view> seen;
    for (const auto &entry : *all_entries) {
        if (auto idx = multidex_index(entry.name);
            idx.has_value() && seen.insert(entry.name).second)
            dex_entries.emplace_back(*idx, &entry);
    }
    if (dex_entries.empty()) {
        return std::unexpected(AnalysisError{AnalysisError::Code::InvalidApk,
                                             label + ": APK contains no classes.dex"});
    }
    // Ties on the index ("classes2.dex" vs "classes02.dex") are broken by name.
    std::sort(dex_entries.begin(), dex_entries.end(), [](const auto &a, const auto &b) {
        return a.first != b.first ? a.first < b.first : a.second->name < b.second->name;
    });
    std::vector<const raw::zip::Entry *> ordered;
    ordered.reserve(dex_entries.size());
    for (const auto &[idx, entry] : dex_entries)
        ordered.push_back(entry);

    auto impl = std::make_shared<AnalysisContextImpl>();
    std::size_t threads = apk_load_threads(options.threads, ordered);
    if (threads == 1) {
        for (const auto *entry : ordered) {
            auto loaded = load_apk_entry(apk_bytes, *entry, label);
            if (!loaded.has_value())
                return std::unexpected(loaded.error());
            add_loaded_slot(*impl, std::move(*loaded));
        }
        return AnalysisContext(std::move(impl));
    }

    auto results = load_apk_entries_parallel(apk_bytes, ordered, label, threads);
    // Walk in multidex order and stop at the first failure.  Entries are only
    // ever skipped after a failure, so every result read here is set.
    for (auto &result : results) {
        if (result.exception)
            std::rethrow_exception(result.exception);
        if (!result.dex->has_value())
            return std::unexpected(result.dex->error());
        add_loaded_slot(*impl, std::move(**result.dex));
    }
    return AnalysisContext(std::move(impl));
}

std::vector<Class> AnalysisContext::classes() const
{
    std::vector<Class> result;
    for (std::size_t i = 0; i < impl_->dex_files_vec.size(); ++i) {
        const auto &dex = impl_->dex_files_vec[i];
        for (uint32_t j = 0; j < static_cast<uint32_t>(dex.class_defs().size()); ++j) {
            result.emplace_back(impl_, i, j);
        }
    }
    return result;
}

std::vector<std::string_view> AnalysisContext::strings() const
{
    std::vector<std::string_view> result;
    for (std::size_t i = 0; i < impl_->dex_files_vec.size(); ++i) {
        uint32_t count = static_cast<uint32_t>(impl_->dex_files_vec[i].string_ids().size());
        result.reserve(result.size() + count);
        for (uint32_t s = 0; s < count; ++s)
            result.push_back(impl_->string_of(i, s));
    }
    return result;
}

std::optional<Class> AnalysisContext::find_class(std::string_view descriptor) const
{
    auto idx = impl_->find_class_index(descriptor);
    if (!idx.has_value())
        return std::nullopt;
    return Class(impl_, idx->first, idx->second);
}

namespace detail {

void AnalysisContextImpl::build_class_index() const
{
    for (std::size_t i = 0; i < dex_files_vec.size(); ++i) {
        const auto &dex = dex_files_vec[i];
        for (uint32_t j = 0; j < static_cast<uint32_t>(dex.class_defs().size()); ++j) {
            const auto &cdef = dex.class_defs()[j];
            class_index.emplace(std::string(type_descriptor_of(i, cdef.class_idx)),
                                std::make_pair(i, j));
        }
    }
}

std::span<const uint8_t> AnalysisContextImpl::buffer_of(std::size_t dex_idx) const
{
    return slots[dex_idx]->buffer.bytes();
}

std::string_view AnalysisContextImpl::string_of(std::size_t dex_idx, uint32_t string_idx) const
{
    // Out-of-range indices reach here from bytecode (const-string), from
    // lazily-decoded class_data, and from malformed id tables; an empty string
    // is the analysis layer's uniform "malformed pool index" sentinel.
    auto string_ids = dex_files_vec[dex_idx].string_ids();
    if (string_idx >= string_ids.size())
        return {};

    auto &slot = *slots[dex_idx];
    std::lock_guard<std::mutex> lock(slot.strings_mutex);

    auto it = slot.string_cache.find(string_idx);
    if (it != slot.string_cache.end())
        return it->second;

    // Decode this one string from its data offset.  A malformed entry caches
    // as empty rather than aborting the whole analysis.
    uint32_t data_off = string_ids[string_idx];
    std::string value;
    if (auto r = raw::parser::parse_string(slot.buffer.bytes(), data_off); r.has_value())
        value = std::move(r->first);

    auto [ins, _] = slot.string_cache.emplace(string_idx, std::move(value));
    return ins->second;
}

std::string_view AnalysisContextImpl::type_descriptor_of(std::size_t dex_idx,
                                                         uint32_t type_idx) const
{
    auto type_ids = dex_files_vec[dex_idx].type_ids();
    if (type_idx >= type_ids.size())
        return {};
    return string_of(dex_idx, type_ids[type_idx]);
}

const raw::MethodIdItem *AnalysisContextImpl::method_item(std::size_t dex_idx,
                                                          uint32_t method_index) const
{
    auto items = dex_files_vec[dex_idx].method_ids();
    return method_index < items.size() ? &items[method_index] : nullptr;
}

const raw::FieldIdItem *AnalysisContextImpl::field_item(std::size_t dex_idx,
                                                        uint32_t field_index) const
{
    auto items = dex_files_vec[dex_idx].field_ids();
    return field_index < items.size() ? &items[field_index] : nullptr;
}

const raw::ProtoIdItem *AnalysisContextImpl::proto_item(std::size_t dex_idx,
                                                        uint32_t proto_index) const
{
    auto items = dex_files_vec[dex_idx].proto_ids();
    return proto_index < items.size() ? &items[proto_index] : nullptr;
}

const raw::ClassDataItem *AnalysisContextImpl::get_class_data(std::size_t dex_idx,
                                                              uint32_t class_data_off) const
{
    if (class_data_off == 0)
        return nullptr;

    auto &slot = *slots[dex_idx];
    std::lock_guard<std::mutex> lock(slot.class_data_mutex);

    auto it = slot.class_data_cache.find(class_data_off);
    if (it != slot.class_data_cache.end())
        return &it->second;

    auto result = raw::parser::parse_class_data(slot.buffer.bytes(), class_data_off);
    if (!result.has_value())
        return nullptr;

    auto [ins_it, ok] = slot.class_data_cache.emplace(class_data_off, std::move(result.value()));
    return &ins_it->second;
}

const raw::CodeItem *AnalysisContextImpl::get_code_item(std::size_t dex_idx,
                                                        uint32_t code_off) const
{
    if (code_off == 0)
        return nullptr;

    auto &slot = *slots[dex_idx];
    std::lock_guard<std::mutex> lock(slot.code_item_mutex);

    auto it = slot.code_item_cache.find(code_off);
    if (it != slot.code_item_cache.end())
        return &it->second;

    auto result = raw::parser::parse_code_item(slot.buffer.bytes(), code_off);
    if (!result.has_value())
        return nullptr;

    auto [ins_it, ok] = slot.code_item_cache.emplace(code_off, std::move(result.value()));
    return &ins_it->second;
}

std::span<const Instruction> AnalysisContextImpl::get_instructions(std::size_t dex_idx,
                                                                   uint32_t code_off) const
{
    if (code_off == 0)
        return {};

    const auto *ci = get_code_item(dex_idx, code_off);
    if (!ci)
        return {};

    auto &slot = *slots[dex_idx];
    std::lock_guard<std::mutex> lock(slot.instructions_mutex);

    auto it = slot.instructions_cache.find(code_off);
    if (it != slot.instructions_cache.end())
        return it->second;

    auto [ins_it, ok] = slot.instructions_cache.emplace(code_off, decode_instructions(ci->insns));
    return ins_it->second;
}

const Cfg &AnalysisContextImpl::get_cfg(std::size_t dex_idx, uint32_t code_off) const
{
    static const Cfg kEmpty{};
    if (code_off == 0)
        return kEmpty;

    const auto *ci = get_code_item(dex_idx, code_off);
    if (!ci)
        return kEmpty;

    auto insns = get_instructions(dex_idx, code_off);

    auto &slot = *slots[dex_idx];
    std::lock_guard<std::mutex> lock(slot.cfg_mutex);

    auto it = slot.cfg_cache.find(code_off);
    if (it != slot.cfg_cache.end())
        return it->second;

    auto [ins_it, ok] = slot.cfg_cache.emplace(code_off, build_cfg(*ci, insns));
    return ins_it->second;
}

std::optional<std::pair<std::size_t, uint32_t>>
AnalysisContextImpl::find_class_index(std::string_view descriptor) const
{
    std::call_once(class_index_flag, [this] { build_class_index(); });

    auto it = class_index.find(std::string(descriptor));
    if (it == class_index.end())
        return std::nullopt;
    return it->second;
}

const CallGraph &AnalysisContextImpl::get_call_graph() const
{
    std::call_once(call_graph_flag,
                   [this] { call_graph_cache.emplace(build_call_graph(shared_from_this())); });
    return *call_graph_cache;
}

const ClassHierarchy &AnalysisContextImpl::get_class_hierarchy() const
{
    std::call_once(class_hierarchy_flag,
                   [this] { class_hierarchy_cache.emplace(build_class_hierarchy(*this)); });
    return *class_hierarchy_cache;
}

const Xrefs &AnalysisContextImpl::get_xrefs() const
{
    std::call_once(xrefs_flag, [this] { xrefs_cache.emplace(build_xrefs(shared_from_this())); });
    return *xrefs_cache;
}

const std::string &AnalysisContextImpl::proto_descriptor_of(std::size_t dex_idx,
                                                            uint32_t proto_idx) const
{
    static const std::string kEmpty;
    auto proto_ids = dex_files_vec[dex_idx].proto_ids();
    if (proto_idx >= proto_ids.size())
        return kEmpty;

    uint64_t key = (static_cast<uint64_t>(dex_idx) << 32) | proto_idx;
    {
        std::lock_guard<std::mutex> lock(proto_cache_mutex);
        if (auto it = proto_descriptor_cache.find(key); it != proto_descriptor_cache.end())
            return it->second;
    }

    const auto &proto = proto_ids[proto_idx];
    std::string out;
    out.push_back('(');

    if (proto.parameters_off != 0) {
        auto type_list = raw::parser::parse_type_list(buffer_of(dex_idx), proto.parameters_off);
        if (type_list.has_value()) {
            for (auto type_idx : type_list->type_ids)
                out.append(type_descriptor_of(dex_idx, type_idx));
        }
        else {
            // Surface the failure in the descriptor so a half-parsed proto
            // can't silently match an unrelated parameterless method.
            out.append("<unparseable>");
        }
    }

    out.push_back(')');
    out.append(type_descriptor_of(dex_idx, proto.return_type_idx));

    // emplace is a no-op if another thread raced us to the same key; either
    // way the returned reference is node-stable for the impl's lifetime.
    std::lock_guard<std::mutex> lock(proto_cache_mutex);
    return proto_descriptor_cache.emplace(key, std::move(out)).first->second;
}

MethodId AnalysisContextImpl::method_id_of(std::size_t dex_idx, uint32_t method_index) const
{
    auto method_ids = dex_files_vec[dex_idx].method_ids();
    if (method_index >= method_ids.size())
        return MethodId{};
    const auto &mid = method_ids[method_index];
    return MethodId{
        std::string(type_descriptor_of(dex_idx, mid.class_idx)),
        std::string(string_of(dex_idx, mid.name_idx)),
        proto_descriptor_of(dex_idx, mid.proto_idx),
    };
}

FieldId AnalysisContextImpl::field_id_of(std::size_t dex_idx, uint32_t field_index) const
{
    auto field_ids = dex_files_vec[dex_idx].field_ids();
    if (field_index >= field_ids.size())
        return FieldId{};
    const auto &fid = field_ids[field_index];
    return FieldId{
        std::string(type_descriptor_of(dex_idx, fid.class_idx)),
        std::string(string_of(dex_idx, fid.name_idx)),
        std::string(type_descriptor_of(dex_idx, fid.type_idx)),
    };
}

} // namespace detail

const CallGraph &AnalysisContext::call_graph() const { return impl_->get_call_graph(); }

const ClassHierarchy &AnalysisContext::class_hierarchy() const
{
    return impl_->get_class_hierarchy();
}

const Xrefs &AnalysisContext::xrefs() const { return impl_->get_xrefs(); }

} // namespace dex
