#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "analysis_context.hpp"
#include "annotations.hpp"
#include "call_graph.hpp"
#include "cfg.hpp"
#include "class_hierarchy.hpp"
#include "instruction.hpp"
#include "raw/types.hpp"
#include "xrefs.hpp"

namespace dex::detail {

/** Owns a DEX file's bytes either as heap memory or a read-only file mapping.
 *
 *  Memory-mapped buffers let the OS page in only the regions an analysis
 *  actually touches (and evict them under pressure), so opening a large DEX
 *  costs address space, not resident memory.  Decompressed APK entries have
 *  no backing file and use the heap variant. */
class ManagedBuffer
{
  public:
    ManagedBuffer() = default;
    explicit ManagedBuffer(std::vector<uint8_t> heap);
    ~ManagedBuffer();

    ManagedBuffer(const ManagedBuffer &) = delete;
    ManagedBuffer &operator=(const ManagedBuffer &) = delete;
    ManagedBuffer(ManagedBuffer &&other) noexcept;
    ManagedBuffer &operator=(ManagedBuffer &&other) noexcept;

    /** Map @p path read-only.  Returns nullopt if the file cannot be opened,
     *  is empty, or mapping is unsupported — callers fall back to heap I/O. */
    static std::optional<ManagedBuffer> map_file(const std::string &path);

    std::span<const uint8_t> bytes() const { return {data_, size_}; }

  private:
    std::vector<uint8_t> heap_;
    const uint8_t *data_ = nullptr;
    std::size_t size_ = 0;
    void *map_addr_ = nullptr; ///< non-null iff mmap-owned
};

/** Private implementation backing AnalysisContext.  Held via shared_ptr by both
 *  the AnalysisContext wrapper and every handle (Class / Method / Field / ClassRef)
 *  derived from it, so handles remain valid for as long as any reference is alive.
 *
 *  Inherits enable_shared_from_this so internals (e.g. the call-graph builder)
 *  can capture a shared_ptr to themselves. */
struct AnalysisContextImpl : public std::enable_shared_from_this<AnalysisContextImpl>
{
    struct DexSlot
    {
        ManagedBuffer buffer;
        mutable std::mutex class_data_mutex;
        mutable std::unordered_map<uint32_t, raw::ClassDataItem> class_data_cache;
        mutable std::mutex code_item_mutex;
        mutable std::unordered_map<uint32_t, raw::CodeItem> code_item_cache;
        mutable std::mutex instructions_mutex;
        mutable std::unordered_map<uint32_t, std::vector<Instruction>> instructions_cache;
        mutable std::mutex cfg_mutex;
        mutable std::unordered_map<uint32_t, Cfg> cfg_cache;
        /** Lazily decoded strings, keyed by string index.  The loaders parse
         *  with decode_strings = false; string_of() fills this on demand. */
        mutable std::mutex strings_mutex;
        mutable std::unordered_map<uint32_t, std::string> string_cache;
        /** The class_def lookup behind class_def_of_type(), built on its
         *  first call.  class_def_by_type[type_idx] is the index of the first
         *  class_def that names type_idx, or kNoClassDef.  A class_def whose
         *  type index is past the type table (a damaged file) cannot go in
         *  that table; it goes in class_def_by_bad_type as a (type_idx,
         *  class_def index) pair, sorted. */
        mutable std::once_flag class_def_table_flag;
        mutable std::vector<uint32_t> class_def_by_type;
        mutable std::vector<std::pair<uint32_t, uint32_t>> class_def_by_bad_type;

        explicit DexSlot(ManagedBuffer buf) : buffer(std::move(buf)) {}
    };

    std::vector<raw::DexFile> dex_files_vec;
    std::vector<std::unique_ptr<DexSlot>> slots;

    /** Threads the whole-program builders (the call graph, the
     *  cross-references) may use: 0 = automatic, 1 = the calling thread only.
     *  Set from ApkLoadOptions::threads by the APK loader. */
    unsigned threads = 0;

    /** `threads`, or the number of hardware threads when it is 0. */
    std::size_t thread_budget() const;

    /** Run fn(dex_idx) once for every loaded DEX file, on up to
     *  thread_budget() threads with the calling thread among them, biggest
     *  files first.  With the automatic thread count (`threads` 0) it stays
     *  on the calling thread when the files besides the biggest hold less than
     *  1 MiB: starting threads would cost more than the work.  An exception thrown by fn is
     * rethrown here, after every thread has stopped.  fn must only touch per-DEX state or
     * thread-safe impl accessors, and a builder must not let its result depend on the order in
     * which the DEX files finish. */
    void for_each_dex_parallel(const std::function<void(std::size_t)> &fn) const;

    /** Hashes a std::string key and a std::string_view query the same way,
     *  so class_index lookups do not copy the descriptor. */
    struct DescriptorHash
    {
        using is_transparent = void;
        std::size_t operator()(std::string_view s) const noexcept
        {
            return std::hash<std::string_view>{}(s);
        }
    };

    /** Class descriptor -> (dex_idx, class_def_idx) of the definition the
     *  context uses for that class.  See is_winning_definition() for the rule
     *  when a class is defined more than once. */
    mutable std::once_flag class_index_flag;
    mutable std::unordered_map<std::string, std::pair<std::size_t, uint32_t>, DescriptorHash,
                               std::equal_to<>>
        class_index;

    mutable std::once_flag call_graph_flag;
    mutable std::optional<CallGraph> call_graph_cache;

    mutable std::once_flag class_hierarchy_flag;
    mutable std::optional<ClassHierarchy> class_hierarchy_cache;

    mutable std::once_flag xrefs_flag;
    mutable std::optional<Xrefs> xrefs_cache;

    /** Full "(...)ret" prototype descriptors, keyed by (dex_idx << 32) | proto_idx. */
    mutable std::mutex proto_cache_mutex;
    mutable std::unordered_map<uint64_t, std::string> proto_descriptor_cache;

    void build_class_index() const;

    const std::vector<raw::DexFile> &dex_files() const { return dex_files_vec; }
    std::span<const uint8_t> buffer_of(std::size_t dex_idx) const;
    std::string_view string_of(std::size_t dex_idx, uint32_t string_idx) const;
    std::string_view type_descriptor_of(std::size_t dex_idx, uint32_t type_idx) const;

    /** Bounds-checked id-table item access: nullptr when the index is out of
     *  range (e.g. a method/field index decoded from malformed class_data).
     *  Handles must use these instead of indexing the raw spans directly. */
    const raw::MethodIdItem *method_item(std::size_t dex_idx, uint32_t method_index) const;
    const raw::FieldIdItem *field_item(std::size_t dex_idx, uint32_t field_index) const;
    const raw::ProtoIdItem *proto_item(std::size_t dex_idx, uint32_t proto_index) const;

    const raw::ClassDataItem *get_class_data(std::size_t dex_idx, uint32_t class_data_off) const;
    const raw::CodeItem *get_code_item(std::size_t dex_idx, uint32_t code_off) const;

    /** The code item at @p code_off without adding it to the cache: the
     *  cached one when an earlier get_code_item() call cached it, else one
     *  parsed into @p scratch.  nullptr when @p code_off is 0 or the item does
     *  not parse, exactly when get_code_item() returns nullptr.  For the
     *  whole-program builders, which read every method once and must not keep
     *  every code item alive for the context's lifetime. */
    const raw::CodeItem *peek_code_item(std::size_t dex_idx, uint32_t code_off,
                                        std::optional<raw::CodeItem> &scratch) const;
    std::span<const Instruction> get_instructions(std::size_t dex_idx, uint32_t code_off) const;
    const Cfg &get_cfg(std::size_t dex_idx, uint32_t code_off) const;

    /** The definition the context uses for the class @p descriptor, as
     *  (dex_idx, class_def_idx), or nullopt when no loaded DEX defines it. */
    std::optional<std::pair<std::size_t, uint32_t>>
    find_class_index(std::string_view descriptor) const;

    /** True when class_defs[@p class_def_idx] of DEX @p dex_idx is the
     *  definition the context uses for its class.
     *
     *  When a class is defined more than once, the first definition wins: the
     *  one in the earliest loaded DEX file (load order is multidex order for
     *  an APK: classes.dex, classes2.dex, ...), and inside one DEX file the
     *  first class_def.  That is what Android does: an app's class loader asks
     *  its DEX files in order and stops at the first one that defines the
     *  class (DexPathList.findClass in libcore, and
     *  ClassLinker::FindClassInBaseDexClassLoaderClassPath in ART).  The later
     *  definitions are never loaded, so their code never runs.
     *
     *  find_class(), ClassRef::resolve(), the class hierarchy, the call graph
     *  and the cross-references all follow this rule.  classes() still lists
     *  every definition. */
    bool is_winning_definition(std::size_t dex_idx, uint32_t class_def_idx) const;

    const CallGraph &get_call_graph() const;
    const ClassHierarchy &get_class_hierarchy() const;
    const Xrefs &get_xrefs() const;

    /** class_defs index (within the SAME dex) whose class_idx == type_idx,
     *  or nullopt if that type is not defined in this DEX.  Used by Method /
     *  Field handles to reach their declaring class's annotations directory
     *  and static values without cross-DEX name lookups.
     *
     *  When several class_defs name the same type (a damaged file), the first
     *  one in class_defs order wins.  A type index past the type table still
     *  finds a class_def that names that same index.  The lookup table is
     *  built on the first call for each DEX (std::call_once), so loading does
     *  not pay for it and concurrent readers are safe. */
    std::optional<uint32_t> class_def_of_type(std::size_t dex_idx, uint32_t type_idx) const;

    /** Marks a type index that no class_def names in DexSlot::class_def_by_type.
     *  A real class_defs index is always smaller: there are at most
     *  UINT32_MAX class_defs, numbered from 0. */
    static constexpr uint32_t kNoClassDef = UINT32_MAX;

    /** Full prototype descriptor (e.g. "(ILjava/lang/String;)V") for
     *  proto_ids[proto_idx].  Cached; the reference stays valid for the impl's
     *  lifetime. */
    const std::string &proto_descriptor_of(std::size_t dex_idx, uint32_t proto_idx) const;

    /** The same descriptor, built fresh and not cached: no lock is shared with
     *  other DEX files, so threads working on different DEX files do not
     *  contend.  Empty for an out-of-range proto_idx. */
    std::string build_proto_descriptor(std::size_t dex_idx, uint32_t proto_idx) const;

    /** Canonical MethodId for method_ids[method_index]. */
    MethodId method_id_of(std::size_t dex_idx, uint32_t method_index) const;

    /** Canonical FieldId for field_ids[field_index]. */
    FieldId field_id_of(std::size_t dex_idx, uint32_t field_index) const;

    /** method_handles[handle_index], resolved to its kind and member.  An
     *  out-of-range index, or a handle type that is not one of the nine
     *  kinds, gives InvokeStatic with an empty MethodId. */
    AnnotationValue::MethodHandleValue method_handle_of(std::size_t dex_idx,
                                                        uint32_t handle_index) const;
};

// ---- annotation resolution (implemented in annotations.cpp) ----

/** Resolve one raw encoded_value into an owned AnnotationValue, resolving pool
 *  indices via @p impl. */
AnnotationValue resolve_encoded_value(const AnalysisContextImpl &impl, std::size_t dex_idx,
                                      const raw::EncodedValue &value);

/** Resolve a raw encoded_annotation; @p visibility is attached as-is (nested
 *  annotations inherit their parent's visibility). */
Annotation resolve_annotation(const AnalysisContextImpl &impl, std::size_t dex_idx,
                              const raw::EncodedAnnotation &ann, AnnotationVisibility visibility);

/** All annotations of the annotation_set_item at @p set_off, resolved.
 *  Returns empty for set_off == 0 or on any parse failure. */
std::vector<Annotation> annotations_at_set(const AnalysisContextImpl &impl, std::size_t dex_idx,
                                           uint32_t set_off);

} // namespace dex::detail
