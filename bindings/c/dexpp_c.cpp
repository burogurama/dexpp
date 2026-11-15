/* C ABI over the dexpp C++ API: errors, memory, the analysis context,
 * classes, class references, methods and fields.  Compiled as C++23.  The
 * other parts are dexpp_c_code.cpp (bytecode, CFG, references, annotations),
 * dexpp_c_graph.cpp (call graph, hierarchy, cross-references) and
 * dexpp_c_apk.cpp (APK, manifest, resources, signing). */

#include "dexpp.h"

#include <string>
#include <vector>

#include "dexpp_c_internal.hpp"

using namespace dexpp_c;

extern "C" {

/* ---- Errors ------------------------------------------------------------- */

DexppErrorCode dexpp_error_code(const DexppError *error) { return error->code; }

const char *dexpp_error_message(const DexppError *error) { return error->message.c_str(); }

size_t dexpp_error_problem_count(const DexppError *error) { return error->problems.size(); }

const char *dexpp_error_problem_where(const DexppError *error, size_t index)
{
    if (index >= error->problems.size())
        return nullptr;
    return error->problems[index].first.c_str();
}

const char *dexpp_error_problem_message(const DexppError *error, size_t index)
{
    if (index >= error->problems.size())
        return nullptr;
    return error->problems[index].second.c_str();
}

void dexpp_error_free(DexppError *error) { delete error; }

/* ---- Memory ------------------------------------------------------------- */

void dexpp_string_free(char *s) { std::free(s); }

void dexpp_string_array_free(char **strings, size_t count)
{
    if (strings == nullptr)
        return;
    for (size_t i = 0; i < count; ++i)
        std::free(strings[i]);
    std::free(strings);
}

void dexpp_text_clear(DexppText *text) { clear(*text); }

void dexpp_text_array_free(DexppText *texts, size_t count)
{
    if (texts == nullptr)
        return;
    for (size_t i = 0; i < count; ++i)
        clear(texts[i]);
    std::free(texts);
}

void dexpp_buffer_free(uint8_t *buffer) { std::free(buffer); }

void dexpp_index_array_free(uint32_t *indices, size_t /*count*/) { std::free(indices); }

void dexpp_method_id_clear(DexppMethodId *id) { clear(*id); }

void dexpp_field_id_clear(DexppFieldId *id) { clear(*id); }

void dexpp_method_id_array_free(DexppMethodId *ids, size_t count)
{
    if (ids == nullptr)
        return;
    for (size_t i = 0; i < count; ++i)
        clear(ids[i]);
    std::free(ids);
}

/* ---- Loading and the analysis context ----------------------------------- */

DexppContext *dexpp_context_from_dex(const char *path, DexppError **err)
{
    reset(err);
    return guarded<DexppContext *>(err, nullptr, [&]() -> DexppContext * {
        auto r = dex::AnalysisContext::from_dex(path);
        if (!r) {
            set_error(err, r.error());
            return nullptr;
        }
        return new DexppContext{std::move(*r)};
    });
}

DexppContext *dexpp_context_from_dex_files(const char *const *paths, size_t count, DexppError **err)
{
    reset(err);
    return guarded<DexppContext *>(err, nullptr, [&]() -> DexppContext * {
        std::vector<std::string> list;
        list.reserve(count);
        for (size_t i = 0; i < count; ++i)
            list.emplace_back(paths[i]);
        auto r = dex::AnalysisContext::from_dex_files(list);
        if (!r) {
            set_error(err, r.error());
            return nullptr;
        }
        return new DexppContext{std::move(*r)};
    });
}

DexppContext *dexpp_context_from_apk(const char *path, uint32_t threads, DexppError **err)
{
    reset(err);
    return guarded<DexppContext *>(err, nullptr, [&]() -> DexppContext * {
        dex::ApkLoadOptions options;
        options.threads = threads;
        auto r = dex::AnalysisContext::from_apk(path, options);
        if (!r) {
            set_error(err, r.error());
            return nullptr;
        }
        return new DexppContext{std::move(*r)};
    });
}

void dexpp_context_free(DexppContext *ctx) { delete ctx; }

DexppClass **dexpp_context_classes(const DexppContext *ctx, size_t *count)
{
    return handle_array<DexppClass>(ctx->ctx.classes(), count);
}

DexppClass *dexpp_context_find_class(const DexppContext *ctx, const char *descriptor)
{
    auto c = ctx->ctx.find_class(descriptor);
    if (!c)
        return nullptr;
    return new DexppClass{*c};
}

DexppText *dexpp_context_strings(const DexppContext *ctx, size_t *count)
{
    std::vector<std::string_view> strings = ctx->ctx.strings();
    *count = strings.size();
    DexppText *out = alloc_array<DexppText>(strings.size());
    for (size_t i = 0; i < strings.size(); ++i)
        out[i] = text(strings[i]);
    return out;
}

DexppCallGraph *dexpp_context_call_graph(const DexppContext *ctx, DexppError **err)
{
    reset(err);
    return guarded<DexppCallGraph *>(
        err, nullptr, [&]() { return new DexppCallGraph{ctx->ctx, &ctx->ctx.call_graph()}; });
}

DexppClassHierarchy *dexpp_context_class_hierarchy(const DexppContext *ctx, DexppError **err)
{
    reset(err);
    return guarded<DexppClassHierarchy *>(err, nullptr, [&]() {
        return new DexppClassHierarchy{ctx->ctx, &ctx->ctx.class_hierarchy()};
    });
}

DexppXrefs *dexpp_context_xrefs(const DexppContext *ctx, DexppError **err)
{
    reset(err);
    return guarded<DexppXrefs *>(err, nullptr,
                                 [&]() { return new DexppXrefs{ctx->ctx, &ctx->ctx.xrefs()}; });
}

/* ---- Classes ------------------------------------------------------------ */

char *dexpp_class_descriptor(const DexppClass *cls) { return cstr(cls->cls.descriptor()); }

char *dexpp_class_package(const DexppClass *cls) { return cstr(cls->cls.package()); }

DexppClassRef *dexpp_class_superclass(const DexppClass *cls)
{
    auto sup = cls->cls.superclass();
    if (!sup)
        return nullptr;
    return new DexppClassRef{std::move(*sup)};
}

DexppClassRef **dexpp_class_interfaces(const DexppClass *cls, size_t *count)
{
    return handle_array<DexppClassRef>(cls->cls.interfaces(), count);
}

uint32_t dexpp_class_access_flags(const DexppClass *cls)
{
    return static_cast<uint32_t>(cls->cls.access_flags());
}

int dexpp_class_is_interface(const DexppClass *cls) { return cls->cls.is_interface() ? 1 : 0; }

int dexpp_class_is_abstract(const DexppClass *cls) { return cls->cls.is_abstract() ? 1 : 0; }

char *dexpp_class_source_file(const DexppClass *cls)
{
    auto file = cls->cls.source_file();
    if (!file)
        return nullptr;
    return cstr(*file);
}

DexppMethod **dexpp_class_methods(const DexppClass *cls, size_t *count)
{
    return handle_array<DexppMethod>(cls->cls.methods(), count);
}

DexppField **dexpp_class_fields(const DexppClass *cls, size_t *count)
{
    return handle_array<DexppField>(cls->cls.fields(), count);
}

DexppAnnotation *dexpp_class_annotations(const DexppClass *cls, size_t *count)
{
    return annotation_array(cls->cls.annotations(), count);
}

void dexpp_class_free(DexppClass *cls) { delete cls; }

void dexpp_class_array_free(DexppClass **classes, size_t count)
{
    if (classes == nullptr)
        return;
    for (size_t i = 0; i < count; ++i)
        delete classes[i];
    std::free(classes);
}

/* ---- Class references --------------------------------------------------- */

char *dexpp_class_ref_descriptor(const DexppClassRef *ref) { return cstr(ref->ref.descriptor()); }

int dexpp_class_ref_is_resolved(const DexppClassRef *ref) { return ref->ref.is_resolved() ? 1 : 0; }

DexppClass *dexpp_class_ref_resolve(const DexppClassRef *ref)
{
    std::optional<dex::Class> cls = ref->ref.resolve();
    if (!cls.has_value())
        return nullptr;
    return new DexppClass{std::move(*cls)};
}

void dexpp_class_ref_free(DexppClassRef *ref) { delete ref; }

void dexpp_class_ref_array_free(DexppClassRef **refs, size_t count)
{
    if (refs == nullptr)
        return;
    for (size_t i = 0; i < count; ++i)
        delete refs[i];
    std::free(refs);
}

/* ---- Methods: identity and flags ---------------------------------------- */

char *dexpp_method_name(const DexppMethod *m) { return cstr(m->method.name()); }

char *dexpp_method_proto(const DexppMethod *m) { return cstr(m->method.proto()); }

DexppClassRef *dexpp_method_declaring_class(const DexppMethod *m)
{
    return new DexppClassRef{m->method.declaring_class()};
}

void dexpp_method_id(const DexppMethod *m, DexppMethodId *out) { *out = method_id(m->method.id()); }

char *dexpp_method_return_type(const DexppMethod *m) { return cstr(m->method.return_type()); }

char **dexpp_method_parameters(const DexppMethod *m, size_t *count)
{
    return string_array(m->method.parameters(), count);
}

uint32_t dexpp_method_access_flags(const DexppMethod *m)
{
    return static_cast<uint32_t>(m->method.access_flags());
}

int dexpp_method_has_code(const DexppMethod *m) { return m->method.has_code() ? 1 : 0; }

int dexpp_method_is_static(const DexppMethod *m) { return m->method.is_static() ? 1 : 0; }

int dexpp_method_is_constructor(const DexppMethod *m) { return m->method.is_constructor() ? 1 : 0; }

uint16_t dexpp_method_register_count(const DexppMethod *m) { return m->method.register_count(); }

void dexpp_method_free(DexppMethod *m) { delete m; }

void dexpp_method_array_free(DexppMethod **methods, size_t count)
{
    if (methods == nullptr)
        return;
    for (size_t i = 0; i < count; ++i)
        delete methods[i];
    std::free(methods);
}

/* ---- Fields ------------------------------------------------------------- */

char *dexpp_field_name(const DexppField *f) { return cstr(f->field.name()); }

DexppClassRef *dexpp_field_declaring_class(const DexppField *f)
{
    return new DexppClassRef{f->field.declaring_class()};
}

char *dexpp_field_type(const DexppField *f) { return cstr(f->field.type()); }

void dexpp_field_id(const DexppField *f, DexppFieldId *out) { *out = field_id(f->field.id()); }

uint32_t dexpp_field_access_flags(const DexppField *f)
{
    return static_cast<uint32_t>(f->field.access_flags());
}

DexppAnnotation *dexpp_field_annotations(const DexppField *f, size_t *count)
{
    return annotation_array(f->field.annotations(), count);
}

int dexpp_field_initial_value(const DexppField *f, DexppValue *out)
{
    auto v = f->field.initial_value();
    if (!v)
        return 0;
    *out = value(*v);
    return 1;
}

void dexpp_field_free(DexppField *f) { delete f; }

void dexpp_field_array_free(DexppField **fields, size_t count)
{
    if (fields == nullptr)
        return;
    for (size_t i = 0; i < count; ++i)
        delete fields[i];
    std::free(fields);
}

} // extern "C"
