/* C ABI over the dexpp C++ API: the APK, its manifest, resources, archive
 * entries and signing information.  Compiled as C++23. */

#include "dexpp.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dexpp_c_internal.hpp"

using namespace dexpp_c;

namespace {

/** 1 / 0 for an explicit boolean, -1 when it is absent or unknown. */
int tri_state(std::optional<bool> v)
{
    if (!v)
        return -1;
    return *v ? 1 : 0;
}

char *optional_string(std::optional<std::string_view> v)
{
    if (!v)
        return nullptr;
    return cstr(*v);
}

DexppComponent component(const dex::apk::Component &c)
{
    DexppComponent out{};
    out.name = cstr(c.name);
    out.name_reference = c.name_reference.value_or(0);
    out.name_unknown = c.name_unknown ? 1 : 0;
    out.target_activity = c.target_activity ? cstr(*c.target_activity) : nullptr;
    out.kind = component_kind(c.kind);
    out.exported = tri_state(c.exported);
    out.exported_reference = c.exported_reference.value_or(0);
    out.exported_unknown = c.exported_unknown ? 1 : 0;
    out.exported_varies = c.exported_varies ? 1 : 0;
    out.exported_default = tri_state(c.exported_default);
    out.permission = c.permission ? cstr(*c.permission) : nullptr;
    out.permission_reference = c.permission_reference.value_or(0);
    out.permission_unknown = c.permission_unknown ? 1 : 0;
    out.permission_inherited = c.permission_inherited ? 1 : 0;
    out.read_permission = c.read_permission ? cstr(*c.read_permission) : nullptr;
    out.read_permission_reference = c.read_permission_reference.value_or(0);
    out.read_permission_unknown = c.read_permission_unknown ? 1 : 0;
    out.write_permission = c.write_permission ? cstr(*c.write_permission) : nullptr;
    out.write_permission_reference = c.write_permission_reference.value_or(0);
    out.write_permission_unknown = c.write_permission_unknown ? 1 : 0;
    out.path_permission_count = c.path_permissions.size();
    out.path_permissions = alloc_array<DexppPathPermission>(c.path_permissions.size());
    for (std::size_t i = 0; i < c.path_permissions.size(); ++i) {
        const dex::apk::PathPermission &p = c.path_permissions[i];
        DexppPathPermission &o = out.path_permissions[i];
        o.path = cstr(p.path);
        o.match = path_match(p.match);
        o.path_unknown = p.path_unknown ? 1 : 0;
        o.read_permission = p.read_permission ? cstr(*p.read_permission) : nullptr;
        o.read_permission_unknown = p.read_permission_unknown ? 1 : 0;
        o.write_permission = p.write_permission ? cstr(*p.write_permission) : nullptr;
        o.write_permission_unknown = p.write_permission_unknown ? 1 : 0;
    }
    out.guarded_by_signature_permission = c.guarded_by_signature_permission ? 1 : 0;
    out.is_exported = c.is_exported() ? 1 : 0;
    out.is_reachable = c.is_reachable() ? 1 : 0;
    out.intent_filter_count = c.intent_filters.size();
    out.intent_filters = alloc_array<DexppIntentFilter>(c.intent_filters.size());
    for (std::size_t i = 0; i < c.intent_filters.size(); ++i) {
        const dex::apk::IntentFilter &f = c.intent_filters[i];
        DexppIntentFilter &o = out.intent_filters[i];
        o.actions = string_array(f.actions, &o.action_count);
        o.categories = string_array(f.categories, &o.category_count);
        o.is_launcher = f.is_launcher() ? 1 : 0;
    }
    return out;
}

void clear(DexppComponent &c)
{
    std::free(c.name);
    std::free(c.target_activity);
    std::free(c.permission);
    std::free(c.read_permission);
    std::free(c.write_permission);
    for (std::size_t i = 0; c.intent_filters != nullptr && i < c.intent_filter_count; ++i) {
        dexpp_string_array_free(c.intent_filters[i].actions, c.intent_filters[i].action_count);
        dexpp_string_array_free(c.intent_filters[i].categories, c.intent_filters[i].category_count);
    }
    std::free(c.intent_filters);
    for (std::size_t i = 0; c.path_permissions != nullptr && i < c.path_permission_count; ++i) {
        std::free(c.path_permissions[i].path);
        std::free(c.path_permissions[i].read_permission);
        std::free(c.path_permissions[i].write_permission);
    }
    std::free(c.path_permissions);
    c = DexppComponent{};
}

} // namespace

extern "C" {

DexppApk *dexpp_apk_open(const char *path, DexppError **err)
{
    reset(err);
    return guarded<DexppApk *>(err, nullptr, [&]() -> DexppApk * {
        auto r = dex::apk::Apk::open(path);
        if (!r) {
            set_error(err, r.error());
            return nullptr;
        }
        return new DexppApk{std::move(*r)};
    });
}

DexppApk *dexpp_apk_open_buffer(const uint8_t *bytes, size_t len, const char *label,
                                DexppError **err)
{
    reset(err);
    return guarded<DexppApk *>(err, nullptr, [&]() -> DexppApk * {
        const std::string name = label != nullptr ? label : "<apk>";
        if (bytes == nullptr && len != 0) {
            set_error(err, DEXPP_ERROR_INVALID_APK,
                      name + ": no data (a NULL pointer with " + std::to_string(len) + " bytes)");
            return nullptr;
        }
        // The span overload copies, so the caller keeps ownership of bytes.
        // A NULL pointer with a length of 0 is a valid empty span.
        auto r = dex::apk::Apk::from_buffer(std::span<const uint8_t>(bytes, len), name);
        if (!r) {
            set_error(err, r.error());
            return nullptr;
        }
        return new DexppApk{std::move(*r)};
    });
}

void dexpp_apk_free(DexppApk *apk) { delete apk; }

char **dexpp_apk_entries(const DexppApk *apk, size_t *count)
{
    return string_array(apk->apk.entries(), count);
}

int dexpp_apk_read(const DexppApk *apk, const char *name, uint8_t **out, size_t *length)
{
    std::optional<std::vector<uint8_t>> bytes = apk->apk.read(name);
    if (!bytes) {
        *out = nullptr;
        *length = 0;
        return 0;
    }
    *length = bytes->size();
    *out = alloc_array<uint8_t>(bytes->size());
    if (!bytes->empty())
        std::memcpy(*out, bytes->data(), bytes->size());
    return 1;
}

char *dexpp_apk_package(const DexppApk *apk)
{
    const dex::apk::Manifest *m = apk->apk.manifest();
    if (m == nullptr)
        return nullptr;
    return cstr(m->package());
}

int dexpp_apk_manifest(const DexppApk *apk, DexppManifest *out)
{
    const dex::apk::Manifest *m = apk->apk.manifest();
    if (m == nullptr)
        return 0;
    DexppManifest r{};
    r.package = cstr(m->package());
    r.has_version_code = m->version_code().has_value() ? 1 : 0;
    r.version_code = m->version_code().value_or(0);
    r.version_code_reference = m->version_code_reference().value_or(0);
    r.version_code_unknown = m->version_code_unknown() ? 1 : 0;
    r.version_name = optional_string(m->version_name());
    r.version_name_reference = m->version_name_reference().value_or(0);
    r.version_name_unknown = m->version_name_unknown() ? 1 : 0;
    r.has_min_sdk = m->min_sdk().has_value() ? 1 : 0;
    r.min_sdk = m->min_sdk().value_or(0);
    r.min_sdk_reference = m->min_sdk_reference().value_or(0);
    r.min_sdk_unknown = m->min_sdk_unknown() ? 1 : 0;
    r.has_target_sdk = m->target_sdk().has_value() ? 1 : 0;
    r.target_sdk = m->target_sdk().value_or(0);
    r.target_sdk_reference = m->target_sdk_reference().value_or(0);
    r.target_sdk_unknown = m->target_sdk_unknown() ? 1 : 0;
    r.permissions = string_array(m->permissions(), &r.permission_count);

    const std::vector<std::string> &declared = m->declared_permissions();
    r.declared_permission_count = declared.size();
    r.declared_permissions = alloc_array<DexppDeclaredPermission>(declared.size());
    for (std::size_t i = 0; i < declared.size(); ++i) {
        std::optional<int> level = m->declared_permission_protection_level(declared[i]);
        r.declared_permissions[i] = DexppDeclaredPermission{
            cstr(declared[i]), level.has_value() ? 1 : 0, static_cast<int32_t>(level.value_or(0)),
            m->declared_permission_protection_level_varies(declared[i]) ? 1 : 0};
    }

    const std::vector<dex::apk::Component> &components = m->components();
    r.component_count = components.size();
    r.components = alloc_array<DexppComponent>(components.size());
    for (std::size_t i = 0; i < components.size(); ++i)
        r.components[i] = component(components[i]);

    r.launcher_activity = optional_string(m->launcher_activity());
    r.debuggable = tri_state(m->debuggable());
    r.debuggable_reference = m->debuggable_reference().value_or(0);
    r.debuggable_unknown = m->debuggable_unknown() ? 1 : 0;
    r.debuggable_varies = m->debuggable_varies() ? 1 : 0;
    r.allow_backup = tri_state(m->allow_backup());
    r.allow_backup_reference = m->allow_backup_reference().value_or(0);
    r.allow_backup_unknown = m->allow_backup_unknown() ? 1 : 0;
    r.allow_backup_varies = m->allow_backup_varies() ? 1 : 0;
    r.application_permission = optional_string(m->application_permission());
    r.application_permission_reference = m->application_permission_reference().value_or(0);
    r.application_permission_unknown = m->application_permission_unknown() ? 1 : 0;
    *out = r;
    return 1;
}

void dexpp_manifest_clear(DexppManifest *manifest)
{
    std::free(manifest->package);
    std::free(manifest->version_name);
    std::free(manifest->launcher_activity);
    std::free(manifest->application_permission);
    dexpp_string_array_free(manifest->permissions, manifest->permission_count);
    for (std::size_t i = 0;
         manifest->declared_permissions != nullptr && i < manifest->declared_permission_count; ++i)
        std::free(manifest->declared_permissions[i].name);
    std::free(manifest->declared_permissions);
    for (std::size_t i = 0; manifest->components != nullptr && i < manifest->component_count; ++i)
        clear(manifest->components[i]);
    std::free(manifest->components);
    *manifest = DexppManifest{};
}

int dexpp_apk_has_resources(const DexppApk *apk) { return apk->apk.resources() != nullptr ? 1 : 0; }

int dexpp_apk_resources_empty(const DexppApk *apk)
{
    const dex::apk::ResourceTable *table = apk->apk.resources();
    return table == nullptr || table->empty() ? 1 : 0;
}

int dexpp_apk_resource_name(const DexppApk *apk, uint32_t id, DexppResourceName *out)
{
    const dex::apk::ResourceTable *table = apk->apk.resources();
    if (table == nullptr)
        return 0;
    std::optional<dex::apk::ResourceName> name = table->name_of(id);
    if (!name)
        return 0;
    *out = DexppResourceName{cstr(name->package), cstr(name->type), cstr(name->entry)};
    return 1;
}

void dexpp_resource_name_clear(DexppResourceName *name)
{
    std::free(name->package);
    std::free(name->type);
    std::free(name->entry);
    *name = DexppResourceName{};
}

int dexpp_apk_resolve_string(const DexppApk *apk, uint32_t id, DexppText *out)
{
    std::optional<std::string_view> value = apk->apk.resolve_string(id);
    if (!value)
        return 0;
    *out = text(*value);
    return 1;
}

DexppResourceValue *dexpp_apk_resource_values(const DexppApk *apk, uint32_t id, size_t *count)
{
    *count = 0;
    const dex::apk::ResourceTable *table = apk->apk.resources();
    if (table == nullptr)
        return nullptr;
    std::vector<dex::apk::ResourceValue> values = table->values(id);
    DexppResourceValue *out = alloc_array<DexppResourceValue>(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        const dex::apk::ResourceValue &v = values[i];
        DexppResourceValue &o = out[i];
        o.configuration = cstr(v.configuration);
        o.type = v.type;
        o.data = v.data;
        if (v.string.has_value())
            o.string = text(*v.string);
        o.boolean = tri_state(v.as_bool());
        std::optional<int64_t> integer = v.as_int();
        o.has_integer = integer.has_value() ? 1 : 0;
        o.integer = integer.value_or(0);
    }
    *count = values.size();
    return out;
}

void dexpp_resource_value_array_free(DexppResourceValue *values, size_t count)
{
    for (std::size_t i = 0; values != nullptr && i < count; ++i) {
        std::free(values[i].configuration);
        dexpp_text_clear(&values[i].string);
    }
    std::free(values);
}

int dexpp_apk_resolve_bool(const DexppApk *apk, uint32_t id, int *out)
{
    const dex::apk::ResourceTable *table = apk->apk.resources();
    if (table == nullptr)
        return 0;
    std::optional<bool> value = table->resolve_bool(id);
    if (!value)
        return 0;
    *out = *value ? 1 : 0;
    return 1;
}

int dexpp_apk_resolve_int(const DexppApk *apk, uint32_t id, int64_t *out)
{
    const dex::apk::ResourceTable *table = apk->apk.resources();
    if (table == nullptr)
        return 0;
    std::optional<int64_t> value = table->resolve_int(id);
    if (!value)
        return 0;
    *out = *value;
    return 1;
}

int dexpp_apk_resource_id(const DexppApk *apk, const char *type, const char *entry, uint32_t *out)
{
    const dex::apk::ResourceTable *table = apk->apk.resources();
    if (table == nullptr)
        return 0;
    std::optional<uint32_t> id = table->id_of(type, entry);
    if (!id)
        return 0;
    *out = *id;
    return 1;
}

void dexpp_apk_signing(const DexppApk *apk, DexppSigningInfo *out)
{
    const dex::apk::SigningInfo &info = apk->apk.signing();
    DexppSigningInfo r{};
    r.v1 = info.v1 ? 1 : 0;
    r.v2 = info.v2 ? 1 : 0;
    r.v3 = info.v3 ? 1 : 0;
    r.certificate_count = info.certificates.size();
    r.certificates = alloc_array<DexppCertificate>(info.certificates.size());
    for (std::size_t i = 0; i < info.certificates.size(); ++i) {
        const dex::apk::Certificate &c = info.certificates[i];
        DexppCertificate &o = r.certificates[i];
        o.der_length = c.der.size();
        o.der = alloc_array<uint8_t>(c.der.size());
        if (!c.der.empty())
            std::memcpy(o.der, c.der.data(), c.der.size());
        o.subject = cstr(c.subject);
        o.issuer = cstr(c.issuer);
        o.serial_hex = cstr(c.serial_hex);
        o.not_before = cstr(c.not_before);
        o.not_after = cstr(c.not_after);
        o.sha1_hex = cstr(c.sha1_hex);
        o.sha256_hex = cstr(c.sha256_hex);
        o.self_signed = c.self_signed() ? 1 : 0;
    }
    *out = r;
}

void dexpp_signing_info_clear(DexppSigningInfo *info)
{
    for (std::size_t i = 0; info->certificates != nullptr && i < info->certificate_count; ++i) {
        DexppCertificate &c = info->certificates[i];
        std::free(c.der);
        std::free(c.subject);
        std::free(c.issuer);
        std::free(c.serial_hex);
        std::free(c.not_before);
        std::free(c.not_after);
        std::free(c.sha1_hex);
        std::free(c.sha256_hex);
    }
    std::free(info->certificates);
    *info = DexppSigningInfo{};
}

DexppContext *dexpp_apk_analysis(const DexppApk *apk, uint32_t threads, DexppError **err)
{
    reset(err);
    return guarded<DexppContext *>(err, nullptr, [&]() -> DexppContext * {
        dex::ApkLoadOptions options;
        options.threads = threads;
        auto r = apk->apk.analysis(options);
        if (!r) {
            set_error(err, r.error());
            return nullptr;
        }
        return new DexppContext{std::move(*r)};
    });
}

} // extern "C"
