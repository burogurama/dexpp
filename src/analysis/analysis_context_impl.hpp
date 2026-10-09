#pragma once

#include <cstdint>
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

        explicit DexSlot(ManagedBuffer buf) : buffer(std::move(buf)) {}
    };

    std::vector<raw::DexFile> dex_files_vec;
    std::vector<std::unique_ptr<DexSlot>> slots;

    mutable std::once_flag class_index_flag;
    mutable std::unordered_map<std::string, std::pair<std::size_t, uint32_t>> class_index;

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
    std::span<const Instruction> get_instructions(std::size_t dex_idx, uint32_t code_off) const;
    const Cfg &get_cfg(std::size_t dex_idx, uint32_t code_off) const;

    std::optional<std::pair<std::size_t, uint32_t>>
    find_class_index(std::string_view descriptor) const;

    const CallGraph &get_call_graph() const;
    const ClassHierarchy &get_class_hierarchy() const;
    const Xrefs &get_xrefs() const;

    /** class_defs index (within the SAME dex) whose class_idx == type_idx,
     *  or nullopt if that type is not defined in this DEX.  Used by Method /
     *  Field handles to reach their declaring class's annotations directory
     *  without cross-DEX name lookups (linear scan; annotation queries are
     *  rare and uncached by design). */
    std::optional<uint32_t> class_def_of_type(std::size_t dex_idx, uint32_t type_idx) const;

    /** Full prototype descriptor (e.g. "(ILjava/lang/String;)V") for
     *  proto_ids[proto_idx].  Cached; the reference stays valid for the impl's
     *  lifetime. */
    const std::string &proto_descriptor_of(std::size_t dex_idx, uint32_t proto_idx) const;

    /** Canonical MethodId for method_ids[method_index]. */
    MethodId method_id_of(std::size_t dex_idx, uint32_t method_index) const;

    /** Canonical FieldId for field_ids[field_index]. */
    FieldId field_id_of(std::size_t dex_idx, uint32_t field_index) const;
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
