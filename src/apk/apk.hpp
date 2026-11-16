#pragma once

/** APK-level entry point: package metadata that exists independently of the
 *  bytecode.  Separate from AnalysisContext on purpose — an Apk is a package
 *  (archive + manifest + resources), an AnalysisContext is its code.  Use
 *  Apk::analysis() to cross from one to the other. */

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/analysis_context.hpp"
#include "apk/manifest.hpp"
#include "apk/resources.hpp"
#include "apk/signing.hpp"
#include "common/error.hpp"

namespace dex::apk {

namespace detail {
struct ApkImpl;
} // namespace detail

/** A loaded Android package.  Owns the archive bytes; lazily decodes the
 *  manifest and lazily builds the code-analysis context on first request.
 *  Cheap to copy (shared ownership of the impl). */
class Apk
{
  public:
    /** Open an APK (ZIP) from disk.
     *  @return The Apk on success.  On failure, an Error with code
     *          FileNotFound if the path does not exist, CannotRead if it
     *          cannot be read (a directory, no permission), or InvalidApk if
     *          it is not a ZIP archive. */
    static std::expected<Apk, Error> open(const std::string &path);

    /** Open an APK (ZIP) whose bytes are already in memory, for example bytes
     *  received over the network or uploaded by a user.  The bytes go through
     *  the same checks as the bytes open() reads from a file, and every
     *  accessor then works the same.
     *
     *  The Apk owns its bytes.  This overload takes the vector by value, so a
     *  caller that passes it with std::move gives its memory to the Apk and
     *  nothing is copied.  An lvalue vector is copied.
     *
     *  @param bytes The content of an APK file.
     *  @param label Names the bytes in error messages, where open() puts the
     *         path, for example the name of the file they came from.  It is
     *         also used for the errors of analysis().
     *  @return The Apk on success.  On failure, an Error with code InvalidApk
     *          if the bytes are not a ZIP archive. */
    static std::expected<Apk, Error> from_buffer(std::vector<uint8_t> bytes,
                                                 const std::string &label = "<apk>");

    /** Open an APK (ZIP) from a view of bytes in memory.  The bytes are
     *  copied, so the caller may free them as soon as this returns.
     *  Otherwise the same as the overload that takes a vector. */
    static std::expected<Apk, Error> from_buffer(std::span<const uint8_t> bytes,
                                                 const std::string &label = "<apk>");

    /** Names of every entry in the archive, in central-directory order. */
    std::vector<std::string> entries() const;

    /** Raw (decompressed) bytes of one archive entry, or nullopt if absent. */
    std::optional<std::vector<uint8_t>> read(std::string_view name) const;

    /** The decoded AndroidManifest.xml.  Parsed once on first call and cached.
     *  nullptr if the APK has no manifest or it fails to decode.  Attributes
     *  given as resource references are resolved through resources() (see
     *  manifest.hpp). */
    const Manifest *manifest() const;

    /** The decoded resources.arsc table.  Parsed once on first call and cached.
     *  nullptr if the APK has no resource table or it fails to decode. */
    const ResourceTable *resources() const;

    /** Convenience: resolve a resource reference id (e.g. the value of
     *  android:label="@string/app_name") to its string, via resources().
     *  nullopt if there is no resource table or the id is not a string. */
    std::optional<std::string_view> resolve_string(uint32_t reference_id) const;

    /** Signing schemes and certificates.  Identification only — digests and
     *  signatures are NOT verified.  Computed once on first call and cached;
     *  an unsigned APK yields a default SigningInfo (is_signed() == false). */
    const SigningInfo &signing() const;

    /** A code-analysis context over every classes*.dex in the APK, in multidex
     *  order, loaded from the bytes the Apk already holds.
     *
     *  @p options works as for AnalysisContext::from_apk: pass
     *  `{.threads = 1}` to keep all the work, the call graph included, on the
     *  calling thread.
     *
     *  The result is cached per value of @p options.  The first call with a
     *  given value builds the context, and every later call with an equal
     *  value returns that same context, so its lazy structures (call graph,
     *  class hierarchy, cross-references) are built once.  A call with
     *  different options builds and keeps a second, separate context, which
     *  holds its own copy of the DEX files; the two give the same answers.
     *  So call it with one setting in a program.  A failure is cached the
     *  same way.
     *
     *  @return The context on success.  On failure, an Error with code
     *          InvalidApk if the APK has no classes.dex or an entry is
     *          damaged, or InvalidDex if a DEX file is malformed.  The
     *          message starts with the path given to open() or the label
     *          given to from_buffer(). */
    std::expected<AnalysisContext, Error> analysis(const ApkLoadOptions &options = {}) const;

  private:
    explicit Apk(std::shared_ptr<detail::ApkImpl> impl);
    std::shared_ptr<detail::ApkImpl> impl_;
};

} // namespace dex::apk
