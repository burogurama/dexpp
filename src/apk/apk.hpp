#pragma once

/** APK-level entry point: package metadata that exists independently of the
 *  bytecode.  Separate from AnalysisContext on purpose — an Apk is a package
 *  (archive + manifest + resources), an AnalysisContext is its code.  Use
 *  Apk::analysis() to cross from one to the other. */

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "analysis/analysis_context.hpp"
#include "analysis/analysis_error.hpp"
#include "apk/manifest.hpp"
#include "apk/resources.hpp"
#include "apk/signing.hpp"

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
     *  @return FileNotFound if the path cannot be opened, InvalidApk if it is
     *          not a ZIP. */
    static std::expected<Apk, AnalysisError> open(const std::string &path);

    /** Names of every entry in the archive, in central-directory order. */
    std::vector<std::string> entries() const;

    /** Raw (decompressed) bytes of one archive entry, or nullopt if absent. */
    std::optional<std::vector<uint8_t>> read(std::string_view name) const;

    /** The decoded AndroidManifest.xml.  Parsed once on first call and cached.
     *  nullopt if the APK has no manifest or it fails to decode. */
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
     *  order.  Built once on first call and cached; InvalidApk if the APK has
     *  no DEX, InvalidDexFile if one is malformed. */
    std::expected<AnalysisContext, AnalysisError> analysis() const;

  private:
    explicit Apk(std::shared_ptr<detail::ApkImpl> impl);
    std::shared_ptr<detail::ApkImpl> impl_;
};

} // namespace dex::apk
