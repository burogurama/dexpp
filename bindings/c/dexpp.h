/* C ABI for the dexpp C++ library.
 *
 * This is the interop boundary for non-C++ consumers (Go via cgo, etc.). The
 * C++ API is handle-based; this header mirrors that with opaque pointers and
 * plain C types. Conventions:
 *
 *   - Functions that can fail take a `char **err`: on failure they return NULL
 *     (or 0) and set *err to a malloc'd message the caller frees with
 *     dexpp_string_free. On success *err is left NULL.
 *   - Strings returned by accessors are malloc'd copies; free them with
 *     dexpp_string_free.
 *   - Handle arrays are malloc'd; free with the matching *_array_free, which
 *     frees every handle and the array itself.
 *   - Class / Method handles keep the underlying analysis alive on their own,
 *     so they remain valid after the owning context handle is freed (mirroring
 *     the C++ handle lifetime model). Free each handle when done.
 */
#ifndef DEXPP_H
#define DEXPP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DexppContext DexppContext;
typedef struct DexppClass DexppClass;
typedef struct DexppMethod DexppMethod;
typedef struct DexppApk DexppApk;

/* Free a string returned by any dexpp_* accessor (or an error string). */
void dexpp_string_free(char *s);

/* ---- AnalysisContext ---------------------------------------------------- */

/* Build an analysis context from an APK or from a single/multiple DEX files.
 * Returns NULL and sets *err on failure. */
DexppContext *dexpp_context_from_apk(const char *path, char **err);
DexppContext *dexpp_context_from_dex(const char *path, char **err);
void dexpp_context_free(DexppContext *ctx);

/* All loaded classes. Returns a malloc'd array of `*count` class handles. */
DexppClass **dexpp_context_classes(const DexppContext *ctx, size_t *count);
void dexpp_class_array_free(DexppClass **classes, size_t count);

/* Locate a class by its "Lpkg/Name;" descriptor; NULL if not present. */
DexppClass *dexpp_context_find_class(const DexppContext *ctx, const char *descriptor);

/* The whole decoded string pool. Malloc'd array of `*count` malloc'd strings. */
char **dexpp_context_strings(const DexppContext *ctx, size_t *count);
void dexpp_string_array_free(char **strings, size_t count);

/* ---- Class -------------------------------------------------------------- */

char *dexpp_class_name(const DexppClass *cls);       /* "Lpkg/Name;" */
char *dexpp_class_package(const DexppClass *cls);    /* "pkg.sub" */
char *dexpp_class_superclass(const DexppClass *cls); /* descriptor, or NULL */
unsigned int dexpp_class_access_flags(const DexppClass *cls);
int dexpp_class_is_interface(const DexppClass *cls);
int dexpp_class_is_abstract(const DexppClass *cls);

/* Interface descriptors implemented by the class. */
char **dexpp_class_interfaces(const DexppClass *cls, size_t *count);
/* Methods of the class. Free with dexpp_method_array_free. */
DexppMethod **dexpp_class_methods(const DexppClass *cls, size_t *count);
void dexpp_method_array_free(DexppMethod **methods, size_t count);

void dexpp_class_free(DexppClass *cls);

/* ---- Method ------------------------------------------------------------- */

char *dexpp_method_name(const DexppMethod *m);
/* Compact "(params)return" descriptor, e.g. "(Ljava/lang/String;I)V". */
char *dexpp_method_descriptor(const DexppMethod *m);
unsigned int dexpp_method_access_flags(const DexppMethod *m);
int dexpp_method_has_code(const DexppMethod *m);
int dexpp_method_is_static(const DexppMethod *m);
/* Number of resolved call sites (invoke targets) in the method. */
size_t dexpp_method_call_count(const DexppMethod *m);

void dexpp_method_free(DexppMethod *m);

/* ---- APK / manifest ----------------------------------------------------- */

DexppApk *dexpp_apk_open(const char *path, char **err);
void dexpp_apk_free(DexppApk *apk);

/* Manifest package name, or NULL if there is no decodable manifest. */
char *dexpp_apk_package(const DexppApk *apk);

/* A code-analysis context over the APK's classes*.dex (multidex order). */
DexppContext *dexpp_apk_analysis(const DexppApk *apk, char **err);

/* One manifest component. `kind`: 0 activity, 1 service, 2 receiver, 3 provider. */
typedef struct
{
    char *name; /* fully-qualified, malloc'd */
    int kind;
    int exported;  /* effective android:exported */
    int reachable; /* exported AND not signature-permission-gated */
} DexppComponent;

DexppComponent *dexpp_apk_components(const DexppApk *apk, size_t *count);
void dexpp_component_array_free(DexppComponent *components, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* DEXPP_H */
