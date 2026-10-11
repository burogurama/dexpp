#include "apk/apk.hpp"

#include <fstream>
#include <mutex>

#include "apk/axml.hpp"
#include "raw/zip.hpp"

namespace dex::apk {

namespace detail {

struct ApkImpl
{
    std::vector<uint8_t> bytes;

    mutable std::once_flag manifest_flag;
    mutable std::optional<Manifest> manifest_cache;

    mutable std::once_flag analysis_flag;
    mutable std::expected<AnalysisContext, AnalysisError> analysis_cache =
        std::unexpected(AnalysisError{AnalysisError::Code::InvalidApk, "uninitialized"});

    mutable std::once_flag resources_flag;
    mutable std::optional<ResourceTable> resources_cache;

    mutable std::once_flag signing_flag;
    mutable SigningInfo signing_cache;

    std::span<const uint8_t> span() const { return bytes; }
};

} // namespace detail

using detail::ApkImpl;

Apk::Apk(std::shared_ptr<ApkImpl> impl) : impl_(std::move(impl)) {}

std::expected<Apk, AnalysisError> Apk::open(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return std::unexpected(
            AnalysisError{AnalysisError::Code::FileNotFound, "Cannot open file: " + path});
    }

    file.seekg(0, std::ios::end);
    auto size = static_cast<std::size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    auto impl = std::make_shared<ApkImpl>();
    impl->bytes.resize(size);
    file.read(reinterpret_cast<char *>(impl->bytes.data()), static_cast<std::streamsize>(size));

    // Validate it is actually a ZIP now, so open() fails fast on a bad file
    // rather than at first entry access.
    auto names = raw::zip::entry_names(impl->span());
    if (!names.has_value()) {
        return std::unexpected(
            AnalysisError{AnalysisError::Code::InvalidApk, path + ": " + names.error().message});
    }

    return Apk(std::move(impl));
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
        impl_->manifest_cache.emplace(std::move(*doc));
    });
    return impl_->manifest_cache.has_value() ? &*impl_->manifest_cache : nullptr;
}

std::expected<AnalysisContext, AnalysisError> Apk::analysis() const
{
    std::call_once(impl_->analysis_flag, [this] {
        impl_->analysis_cache = AnalysisContext::from_apk_buffer(impl_->span());
    });
    return impl_->analysis_cache;
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
