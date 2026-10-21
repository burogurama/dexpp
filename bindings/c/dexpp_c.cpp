/* C ABI implementation over the dexpp C++ API. Compiled as C++23. */

#include "dexpp.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "dex.hpp"

namespace {

/* malloc'd, null-terminated copy of a string_view (the C side frees it). */
char *cstr(std::string_view sv)
{
    char *p = static_cast<char *>(std::malloc(sv.size() + 1));
    if (p == nullptr) {
        return nullptr;
    }
    std::memcpy(p, sv.data(), sv.size());
    p[sv.size()] = '\0';
    return p;
}

} // namespace

struct DexppContext
{
    dex::AnalysisContext ctx;
};
struct DexppClass
{
    dex::Class cls;
};
struct DexppMethod
{
    dex::Method method;
};
struct DexppApk
{
    dex::apk::Apk apk;
};

extern "C" {

void dexpp_string_free(char *s) { std::free(s); }

/* ---- AnalysisContext ---------------------------------------------------- */

DexppContext *dexpp_context_from_apk(const char *path, char **err)
{
    auto r = dex::AnalysisContext::from_apk(path);
    if (!r) {
        if (err != nullptr) {
            *err = cstr(r.error().message);
        }
        return nullptr;
    }
    return new DexppContext{std::move(*r)};
}

DexppContext *dexpp_context_from_dex(const char *path, char **err)
{
    auto r = dex::AnalysisContext::from_dex(path);
    if (!r) {
        if (err != nullptr) {
            *err = cstr(r.error().message);
        }
        return nullptr;
    }
    return new DexppContext{std::move(*r)};
}

void dexpp_context_free(DexppContext *ctx) { delete ctx; }

DexppClass **dexpp_context_classes(const DexppContext *ctx, size_t *count)
{
    auto classes = ctx->ctx.classes();
    *count = classes.size();
    auto **arr = static_cast<DexppClass **>(std::malloc(sizeof(DexppClass *) * classes.size()));
    for (size_t i = 0; i < classes.size(); ++i) {
        arr[i] = new DexppClass{classes[i]};
    }
    return arr;
}

void dexpp_class_array_free(DexppClass **classes, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        delete classes[i];
    }
    std::free(classes);
}

DexppClass *dexpp_context_find_class(const DexppContext *ctx, const char *descriptor)
{
    auto c = ctx->ctx.find_class(descriptor);
    if (!c) {
        return nullptr;
    }
    return new DexppClass{*c};
}

char **dexpp_context_strings(const DexppContext *ctx, size_t *count)
{
    auto strings = ctx->ctx.strings();
    *count = strings.size();
    auto **arr = static_cast<char **>(std::malloc(sizeof(char *) * strings.size()));
    for (size_t i = 0; i < strings.size(); ++i) {
        arr[i] = cstr(strings[i]);
    }
    return arr;
}

void dexpp_string_array_free(char **strings, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        std::free(strings[i]);
    }
    std::free(strings);
}

/* ---- Class -------------------------------------------------------------- */

char *dexpp_class_name(const DexppClass *cls) { return cstr(cls->cls.name()); }
char *dexpp_class_package(const DexppClass *cls) { return cstr(cls->cls.package()); }

char *dexpp_class_superclass(const DexppClass *cls)
{
    auto sup = cls->cls.superclass();
    if (!sup) {
        return nullptr;
    }
    return cstr(sup->descriptor());
}

unsigned int dexpp_class_access_flags(const DexppClass *cls)
{
    return static_cast<unsigned int>(cls->cls.access_flags());
}

int dexpp_class_is_interface(const DexppClass *cls) { return cls->cls.is_interface() ? 1 : 0; }
int dexpp_class_is_abstract(const DexppClass *cls) { return cls->cls.is_abstract() ? 1 : 0; }

char **dexpp_class_interfaces(const DexppClass *cls, size_t *count)
{
    auto ifaces = cls->cls.interfaces();
    *count = ifaces.size();
    auto **arr = static_cast<char **>(std::malloc(sizeof(char *) * ifaces.size()));
    for (size_t i = 0; i < ifaces.size(); ++i) {
        arr[i] = cstr(ifaces[i].descriptor());
    }
    return arr;
}

DexppMethod **dexpp_class_methods(const DexppClass *cls, size_t *count)
{
    auto methods = cls->cls.methods();
    *count = methods.size();
    auto **arr = static_cast<DexppMethod **>(std::malloc(sizeof(DexppMethod *) * methods.size()));
    for (size_t i = 0; i < methods.size(); ++i) {
        arr[i] = new DexppMethod{methods[i]};
    }
    return arr;
}

void dexpp_method_array_free(DexppMethod **methods, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        delete methods[i];
    }
    std::free(methods);
}

void dexpp_class_free(DexppClass *cls) { delete cls; }

/* ---- Method ------------------------------------------------------------- */

char *dexpp_method_name(const DexppMethod *m) { return cstr(m->method.name()); }

char *dexpp_method_descriptor(const DexppMethod *m)
{
    std::string d = "(";
    for (const auto &p : m->method.parameters()) {
        d.append(p.descriptor());
    }
    d += ')';
    d.append(m->method.return_type().descriptor());
    return cstr(d);
}

unsigned int dexpp_method_access_flags(const DexppMethod *m)
{
    return static_cast<unsigned int>(m->method.access_flags());
}

int dexpp_method_has_code(const DexppMethod *m) { return m->method.has_code() ? 1 : 0; }
int dexpp_method_is_static(const DexppMethod *m) { return m->method.is_static() ? 1 : 0; }

size_t dexpp_method_call_count(const DexppMethod *m) { return m->method.calls().size(); }

void dexpp_method_free(DexppMethod *m) { delete m; }

/* ---- APK / manifest ----------------------------------------------------- */

DexppApk *dexpp_apk_open(const char *path, char **err)
{
    auto r = dex::apk::Apk::open(path);
    if (!r) {
        if (err != nullptr) {
            *err = cstr(r.error().message);
        }
        return nullptr;
    }
    return new DexppApk{std::move(*r)};
}

void dexpp_apk_free(DexppApk *apk) { delete apk; }

char *dexpp_apk_package(const DexppApk *apk)
{
    const dex::apk::Manifest *m = apk->apk.manifest();
    if (m == nullptr) {
        return nullptr;
    }
    return cstr(m->package());
}

DexppContext *dexpp_apk_analysis(const DexppApk *apk, char **err)
{
    auto r = apk->apk.analysis();
    if (!r) {
        if (err != nullptr) {
            *err = cstr(r.error().message);
        }
        return nullptr;
    }
    return new DexppContext{std::move(*r)};
}

DexppComponent *dexpp_apk_components(const DexppApk *apk, size_t *count)
{
    const dex::apk::Manifest *m = apk->apk.manifest();
    if (m == nullptr) {
        *count = 0;
        return nullptr;
    }
    const auto &comps = m->components();
    *count = comps.size();
    auto *arr = static_cast<DexppComponent *>(std::malloc(sizeof(DexppComponent) * comps.size()));
    for (size_t i = 0; i < comps.size(); ++i) {
        arr[i].name = cstr(comps[i].name);
        arr[i].kind = static_cast<int>(comps[i].kind);
        arr[i].exported = comps[i].is_exported() ? 1 : 0;
        arr[i].reachable = comps[i].is_reachable() ? 1 : 0;
    }
    return arr;
}

void dexpp_component_array_free(DexppComponent *components, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        std::free(components[i].name);
    }
    std::free(components);
}

} // extern "C"
