#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "analysis_error.hpp"
#include "call_graph.hpp"
#include "class.hpp"
#include "class_hierarchy.hpp"
#include "xrefs.hpp"

namespace dex {

namespace detail {
struct AnalysisContextImpl;
} // namespace detail

/** How an APK's DEX files are loaded by AnalysisContext::from_apk / from_apk_buffer. */
struct ApkLoadOptions
{
    /** Threads used to decompress and parse the DEX entries.  0 = automatic:
     *  one per hardware thread, never more than one per DEX entry.  1 = load
     *  everything on the calling thread.  Extra threads are only started when
     *  at least 3 DEX entries are deflate-compressed; with fewer, starting
     *  them costs more than it saves.  Set this to 1 when the caller already
     *  loads several APKs in parallel, so the machine is not oversubscribed. */
    unsigned threads = 0;
};

/** The entry point for analysis.
 *
 *  AnalysisContext owns one or more parsed DEX files and exposes a high-level API
 *  for querying classes, methods, and fields.  It is the root object that all
 *  Class / Method / Field / ClassRef / TypeDescriptor handles point back to.
 *
 *  Internally the context uses a shared_ptr to a private Impl, making copy cheap
 *  (shared ownership) and the object safe to return from factory functions by value.
 *  Handles obtained from this context keep their own shared_ptr to the Impl, so
 *  they remain valid even after every AnalysisContext wrapper has been destroyed. */
class AnalysisContext
{
  public:
    /** Parse a single DEX file from disk.
     *  @return AnalysisContext on success, or FileNotFound / InvalidDexFile on error. */
    static std::expected<AnalysisContext, AnalysisError> from_dex(const std::string &path);

    /** Parse multiple DEX files (e.g. multi-dex splits) in one context.
     *  Returns on the first failure. */
    static std::expected<AnalysisContext, AnalysisError>
    from_dex_files(const std::vector<std::string> &paths);

    /** Open an APK (ZIP) and load every classes*.dex inside it, in multidex
     *  order (classes.dex, classes2.dex, classes3.dex, ...).  If a name
     *  appears more than once in the archive, only its first entry is loaded.
     *  @return AnalysisContext on success; FileNotFound if the path cannot be
     *          opened, InvalidApk if it is not a ZIP or contains no
     *          classes.dex, InvalidDexFile if an embedded DEX is malformed.
     *          With several bad entries, the error is the one for the first
     *          in multidex order, whatever @p options.threads is. */
    static std::expected<AnalysisContext, AnalysisError>
    from_apk(const std::string &path, const ApkLoadOptions &options = {});

    /** Build a context from an in-memory APK (ZIP) image.  Used by the apk
     *  layer (dex::apk::Apk) so an already-loaded archive is not re-read from
     *  disk.  @p label only prefixes error messages. */
    static std::expected<AnalysisContext, AnalysisError>
    from_apk_buffer(std::span<const uint8_t> apk_bytes, const std::string &label = "<apk>",
                    const ApkLoadOptions &options = {});

    /** All classes across every loaded DEX file, in file / definition order. */
    [[nodiscard]] std::vector<Class> classes() const;

    /** Every string in every loaded DEX's string pool, in (dex, string-index)
     *  order — including strings no bytecode loads (type/member names, debug
     *  data, etc.).  This is the complete pool; `Xrefs::referenced_strings()`
     *  is the subset reachable from const-string instructions.
     *
     *  Decodes and caches the whole pool on demand (an explicitly expensive
     *  call); the returned views are backed by per-DEX caches and stay valid
     *  for the lifetime of the AnalysisContext.  A string present in more than
     *  one DEX appears once per DEX. */
    [[nodiscard]] std::vector<std::string_view> strings() const;

    /** Locate a class by its 'L...;' descriptor; std::nullopt if not present. */
    [[nodiscard]] std::optional<Class> find_class(std::string_view descriptor) const;

    /** Whole-program call graph for every defined method across all loaded
     *  DEX files.  Built lazily on first access and cached; repeated calls
     *  return the same reference. */
    [[nodiscard]] const CallGraph &call_graph() const;

    /** Whole-program class hierarchy: inheritance and interface-implementation
     *  relationships across all loaded DEX files, plus descriptor-only nodes
     *  for every external supertype referenced.  Built lazily and cached. */
    [[nodiscard]] const ClassHierarchy &class_hierarchy() const;

    /** Whole-program cross-reference index: call sites by target method,
     *  field reads/writes, const-string loads, and type references.  Built
     *  lazily on first access and cached; repeated calls return the same
     *  reference. */
    [[nodiscard]] const Xrefs &xrefs() const;

  private:
    friend class ClassRef;

    explicit AnalysisContext(std::shared_ptr<detail::AnalysisContextImpl> impl);

    std::shared_ptr<detail::AnalysisContextImpl> impl_;
};

} // namespace dex
