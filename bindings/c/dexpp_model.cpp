/* C ABI implementation over dex::model. Compiled as C++23. */

#include "dexpp_model.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "dexpp_c_internal.hpp"
#include "model/model.hpp"

using dexpp_c::cstr;
using dexpp_c::reset;
using dexpp_c::set_error;

namespace {

/* The index of the class with this descriptor, or -1. */
long class_index(const dex::model::Dex &dex, std::string_view descriptor)
{
    for (std::size_t i = 0; i < dex.classes.size(); ++i) {
        if (dex.classes[i].type == descriptor) {
            return static_cast<long>(i);
        }
    }
    return -1;
}

} // namespace

struct DexppModelDex
{
    dex::model::Dex dex;
};

extern "C" {

/* ---- Read / write / lifetime -------------------------------------------- */

DexppModelDex *dexpp_model_read(const uint8_t *bytes, size_t len, int debug_info, DexppError **err)
{
    reset(err);
    return dexpp_c::guarded<DexppModelDex *>(err, nullptr, [&]() -> DexppModelDex * {
        dex::model::ReadOptions options;
        options.debug_info = debug_info != 0;
        auto r = dex::model::read(std::span<const uint8_t>(bytes, len), options);
        if (!r) {
            set_error(err, r.error());
            return nullptr;
        }
        return new DexppModelDex{std::move(*r)};
    });
}

DexppModelDex *dexpp_model_read_file(const char *path, int debug_info, DexppError **err)
{
    reset(err);
    return dexpp_c::guarded<DexppModelDex *>(err, nullptr, [&]() -> DexppModelDex * {
        dex::model::ReadOptions options;
        options.debug_info = debug_info != 0;
        auto r = dex::model::read_file(path, options);
        if (!r) {
            set_error(err, r.error());
            return nullptr;
        }
        return new DexppModelDex{std::move(*r)};
    });
}

DexppModelDex *dexpp_model_new(uint32_t version)
{
    auto *m = new DexppModelDex{};
    m->dex.version = version;
    return m;
}

DexppModelDex *dexpp_model_clone(const DexppModelDex *dex) { return new DexppModelDex{dex->dex}; }

void dexpp_model_free(DexppModelDex *dex) { delete dex; }

uint8_t *dexpp_model_write(const DexppModelDex *dex, int debug_info, size_t *out_len,
                           DexppError **err)
{
    reset(err);
    *out_len = 0;
    return dexpp_c::guarded<uint8_t *>(err, nullptr, [&]() -> uint8_t * {
        dex::model::WriteOptions options;
        options.debug_info = debug_info != 0;
        auto r = dex::model::write(dex->dex, options);
        if (!r) {
            set_error(err, r.error());
            return nullptr;
        }
        // A DEX file is never empty (it starts with a 112-byte header).
        auto *buffer = dexpp_c::alloc_array<uint8_t>(r->size());
        std::memcpy(buffer, r->data(), r->size());
        *out_len = r->size();
        return buffer;
    });
}

int dexpp_model_write_file(const DexppModelDex *dex, const char *path, int debug_info,
                           DexppError **err)
{
    reset(err);
    return dexpp_c::guarded<int>(err, 0, [&]() {
        dex::model::WriteOptions options;
        options.debug_info = debug_info != 0;
        auto r = dex::model::write_file(dex->dex, path, options);
        if (!r) {
            set_error(err, r.error());
            return 0;
        }
        return 1;
    });
}

/* ---- Version, pools, validation ----------------------------------------- */

uint32_t dexpp_model_version(const DexppModelDex *dex) { return dex->dex.version; }
void dexpp_model_set_version(DexppModelDex *dex, uint32_t version) { dex->dex.version = version; }

uint32_t dexpp_model_required_version(const DexppModelDex *dex)
{
    return dex::model::required_version(dex->dex);
}

DexppPoolStats dexpp_model_pool_stats(const DexppModelDex *dex)
{
    dex::model::PoolStats s = dex::model::pool_stats(dex->dex);
    return DexppPoolStats{s.strings, s.types,          s.protos,    s.fields,
                          s.methods, s.method_handles, s.call_sites};
}

DexppProblem *dexpp_model_validate(const DexppModelDex *dex, size_t *count)
{
    std::vector<dex::Problem> problems = dex::model::validate(dex->dex);
    *count = problems.size();
    if (problems.empty()) {
        return nullptr;
    }
    auto *arr = static_cast<DexppProblem *>(std::malloc(sizeof(DexppProblem) * problems.size()));
    for (std::size_t i = 0; i < problems.size(); ++i) {
        arr[i].where = cstr(problems[i].where);
        arr[i].message = cstr(problems[i].message);
    }
    return arr;
}

void dexpp_problem_array_free(DexppProblem *problems, size_t count)
{
    for (std::size_t i = 0; i < count; ++i) {
        std::free(problems[i].where);
        std::free(problems[i].message);
    }
    std::free(problems);
}

/* ---- Classes ------------------------------------------------------------ */

size_t dexpp_model_class_count(const DexppModelDex *dex) { return dex->dex.classes.size(); }

char **dexpp_model_class_descriptors(const DexppModelDex *dex, size_t *count)
{
    std::vector<std::string_view> types;
    types.reserve(dex->dex.classes.size());
    for (const dex::model::ClassDef &cls : dex->dex.classes)
        types.push_back(cls.type);
    return dexpp_c::string_array(types, count);
}

int dexpp_model_has_class(const DexppModelDex *dex, const char *descriptor)
{
    return class_index(dex->dex, descriptor) >= 0 ? 1 : 0;
}

int dexpp_model_class_info(const DexppModelDex *dex, const char *descriptor, DexppClassInfo *out)
{
    long i = class_index(dex->dex, descriptor);
    if (i < 0) {
        return 0;
    }
    const dex::model::ClassDef &cls = dex->dex.classes[static_cast<std::size_t>(i)];
    out->descriptor = cstr(cls.type);
    out->access_flags = static_cast<uint32_t>(cls.access);
    out->superclass = cls.superclass ? cstr(*cls.superclass) : nullptr;
    out->source_file = cls.source_file ? cstr(*cls.source_file) : nullptr;
    out->interfaces = dexpp_c::string_array(cls.interfaces, &out->interface_count);
    out->field_count = cls.fields.size();
    out->method_count = cls.methods.size();
    return 1;
}

void dexpp_class_info_clear(DexppClassInfo *info)
{
    std::free(info->descriptor);
    std::free(info->superclass);
    std::free(info->source_file);
    dexpp_string_array_free(info->interfaces, info->interface_count);
    info->descriptor = nullptr;
    info->superclass = nullptr;
    info->source_file = nullptr;
    info->interfaces = nullptr;
    info->interface_count = 0;
}

/* ---- Editing by whole class --------------------------------------------- */

int dexpp_model_move_class(DexppModelDex *dst, DexppModelDex *src, const char *descriptor,
                           DexppError **err)
{
    reset(err);
    if (class_index(dst->dex, descriptor) >= 0) {
        set_error(err, DEXPP_ERROR_MODEL,
                  std::string("destination already has class ") + descriptor);
        return 0;
    }
    long i = class_index(src->dex, descriptor);
    if (i < 0) {
        set_error(err, DEXPP_ERROR_MODEL, std::string("source has no class ") + descriptor);
        return 0;
    }
    auto it = src->dex.classes.begin() + i;
    dst->dex.classes.push_back(std::move(*it));
    src->dex.classes.erase(it);
    return 1;
}

int dexpp_model_copy_class(DexppModelDex *dst, const DexppModelDex *src, const char *descriptor,
                           DexppError **err)
{
    reset(err);
    if (class_index(dst->dex, descriptor) >= 0) {
        set_error(err, DEXPP_ERROR_MODEL,
                  std::string("destination already has class ") + descriptor);
        return 0;
    }
    long i = class_index(src->dex, descriptor);
    if (i < 0) {
        set_error(err, DEXPP_ERROR_MODEL, std::string("source has no class ") + descriptor);
        return 0;
    }
    dst->dex.classes.push_back(src->dex.classes[static_cast<std::size_t>(i)]);
    return 1;
}

int dexpp_model_remove_class(DexppModelDex *dex, const char *descriptor)
{
    long i = class_index(dex->dex, descriptor);
    if (i < 0) {
        return 0;
    }
    dex->dex.classes.erase(dex->dex.classes.begin() + i);
    return 1;
}

/* ---- Renaming ----------------------------------------------------------- */

void dexpp_model_rename_types(DexppModelDex *dex, const char *const *from, const char *const *to,
                              size_t count)
{
    std::unordered_map<std::string, std::string> mapping;
    mapping.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        mapping.emplace(from[i], to[i]);
    }
    dex::model::rename_types(dex->dex, [&](std::string_view type) -> std::optional<std::string> {
        auto it = mapping.find(std::string(type));
        if (it == mapping.end()) {
            return std::nullopt;
        }
        return it->second;
    });
}

} // extern "C"
