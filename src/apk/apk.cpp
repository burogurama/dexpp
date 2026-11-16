#include "apk/apk.hpp"

#include <mutex>

#include "apk/axml.hpp"
#include "raw/file_io.hpp"
#include "raw/zip.hpp"

namespace dex::apk {

namespace detail {

struct ApkImpl
{
    std::vector<uint8_t> bytes;
    /** Prefixes error messages: the path given to open(), or the label given
     *  to from_buffer(). */
    std::string label;

    mutable std::once_flag manifest_flag;
    mutable std::optional<Manifest> manifest_cache;

    /** One analysis per distinct ApkLoadOptions, in the order of first use.
     *  A program normally uses one setting, so a linear search is enough. */
    struct CachedAnalysis
    {
        ApkLoadOptions options;
        std::expected<AnalysisContext, Error> result;
    };
    mutable std::mutex analysis_mutex;
    mutable std::vector<CachedAnalysis> analysis_cache;

    mutable std::once_flag resources_flag;
    mutable std::optional<ResourceTable> resources_cache;

    mutable std::once_flag signing_flag;
    mutable SigningInfo signing_cache;

    std::span<const uint8_t> span() const { return bytes; }
};

} // namespace detail

using detail::ApkImpl;

Apk::Apk(std::shared_ptr<ApkImpl> impl) : impl_(std::move(impl)) {}

namespace {

/** The one place where archive bytes become an ApkImpl, for open() and both
 *  from_buffer() overloads, so bytes from memory get exactly the checks that
 *  bytes from a file get. */
std::expected<std::shared_ptr<ApkImpl>, Error> make_impl(std::vector<uint8_t> bytes,
                                                         std::string label)
{
    auto impl = std::make_shared<ApkImpl>();
    impl->bytes = std::move(bytes);
    impl->label = std::move(label);

    // Validate it is actually a ZIP now, so opening fails fast on bad bytes
    // rather than at first entry access.
    auto names = raw::zip::entry_names(impl->span());
    if (!names.has_value()) {
        return std::unexpected(
            Error{ErrorCode::InvalidApk, impl->label + ": " + names.error().message});
    }
    return impl;
}

} // namespace

std::expected<Apk, Error> Apk::open(const std::string &path)
{
    auto bytes = raw::read_whole_file(path);
    if (!bytes)
        return std::unexpected(std::move(bytes.error()));
    return make_impl(std::move(*bytes), path).transform([](std::shared_ptr<ApkImpl> impl) {
        return Apk(std::move(impl));
    });
}

std::expected<Apk, Error> Apk::from_buffer(std::vector<uint8_t> bytes, const std::string &label)
{
    return make_impl(std::move(bytes), label).transform([](std::shared_ptr<ApkImpl> impl) {
        return Apk(std::move(impl));
    });
}

std::expected<Apk, Error> Apk::from_buffer(std::span<const uint8_t> bytes, const std::string &label)
{
    return from_buffer(std::vector<uint8_t>(bytes.begin(), bytes.end()), label);
}

std::vector<std::string> Apk::entries() const
{
    auto names = raw::zip::entry_names(impl_->span());
    return names.has_value() ? std::move(*names) : std::vector<std::string>{};
}

std::optional<std::vector<uint8_t>> Apk::read(std::string_view name) const
{
    auto data = raw::zip::read_entry(impl_->span(), name);
    if (!data.has_value())
        return std::nullopt;
    return std::move(*data);
}

const Manifest *Apk::manifest() const
{
    std::call_once(impl_->manifest_flag, [this] {
        auto data = raw::zip::read_entry(impl_->span(), "AndroidManifest.xml");
        if (!data.has_value())
            return;
        auto doc = axml::parse(std::span<const uint8_t>(*data));
        if (!doc.has_value())
            return;
        // Boolean and integer attributes may be resource references; the
        // resource table resolves them (resources() has its own once_flag).
        impl_->manifest_cache.emplace(std::move(*doc), resources());
    });
    return impl_->manifest_cache.has_value() ? &*impl_->manifest_cache : nullptr;
}

std::expected<AnalysisContext, Error> Apk::analysis(const ApkLoadOptions &options) const
{
    // The lock is held while the context is built, so two threads asking for
    // the same options build it once, as std::call_once did.  If the build
    // throws (out of memory), nothing is cached and a later call tries again.
    std::lock_guard<std::mutex> lock(impl_->analysis_mutex);
    for (const ApkImpl::CachedAnalysis &cached : impl_->analysis_cache)
        if (cached.options == options)
            return cached.result;
    std::expected<AnalysisContext, Error> result =
        AnalysisContext::from_apk_buffer(impl_->span(), impl_->label, options);
    impl_->analysis_cache.push_back({options, result});
    return result;
}

const ResourceTable *Apk::resources() const
{
    std::call_once(impl_->resources_flag, [this] {
        auto data = raw::zip::read_entry(impl_->span(), "resources.arsc");
        if (!data.has_value())
            return;
        auto table = ResourceTable::parse(std::span<const uint8_t>(*data));
        if (!table.has_value())
            return;
        impl_->resources_cache.emplace(std::move(*table));
    });
    return impl_->resources_cache.has_value() ? &*impl_->resources_cache : nullptr;
}

std::optional<std::string_view> Apk::resolve_string(uint32_t reference_id) const
{
    const ResourceTable *table = resources();
    if (!table)
        return std::nullopt;
    return table->resolve_string(reference_id);
}

const SigningInfo &Apk::signing() const
{
    std::call_once(impl_->signing_flag,
                   [this] { impl_->signing_cache = parse_signing_info(impl_->span()); });
    return impl_->signing_cache;
}

} // namespace dex::apk
