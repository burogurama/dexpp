/**
 * @file dexpp_model.h
 * @brief C API for dex::model, the editable DEX model.
 *
 * Read a DEX file into an in-memory model, change it, and write a DEX file
 * again. The C++ model is a tree of plain structs (src/model/types.hpp); this
 * API keeps it behind one opaque handle, DexppModelDex, and works at the
 * granularity of whole classes:
 *
 * - read a DEX into a model, or make an empty model;
 * - inspect its classes (descriptor, access flags, superclass, interfaces,
 *   source file, field and method counts);
 * - build a new model by moving or copying whole classes between models,
 *   which is how a DEX file is split into several files or several merged;
 * - rename every reference to a class;
 * - validate it, ask the lowest DEX version it needs, count its pools;
 * - write it back to DEX bytes.
 *
 * Editing inside a method is not exposed: a class crosses this boundary
 * whole. Building classes instruction by instruction stays in the C++ API.
 *
 * The conventions are those of dexpp.h (errors, strings, arrays, `_clear`
 * functions), which this header includes. A failed write reports
 * DEXPP_ERROR_INVALID_MODEL with the list of problems, DEXPP_ERROR_POOL_OVERFLOW
 * or DEXPP_ERROR_VERSION_TOO_LOW; a failed move or copy reports
 * DEXPP_ERROR_MODEL.
 */
#ifndef DEXPP_MODEL_H
#define DEXPP_MODEL_H

#include <stddef.h>
#include <stdint.h>

#include "dexpp.h"

#ifdef __cplusplus
extern "C" {
#endif

/** An editable DEX file in memory. */
typedef struct DexppModelDex DexppModelDex;

/* ---- Read / write / lifetime -------------------------------------------- */

/** Read DEX bytes into a model. `debug_info` nonzero keeps line numbers and
 *  local variables. Errors: INVALID_DEX, UNSUPPORTED_DEX. */
DEXPP_C_API DexppModelDex *dexpp_model_read(const uint8_t *bytes, size_t len, int debug_info,
                                            DexppError **err);

/** Read a DEX file into a model. Errors: FILE_NOT_FOUND, CANNOT_READ,
 *  INVALID_DEX, UNSUPPORTED_DEX. */
DEXPP_C_API DexppModelDex *dexpp_model_read_file(const char *path, int debug_info,
                                                 DexppError **err);

/** An empty model of the given DEX version (35, 37, 38, 39 or 40). */
DEXPP_C_API DexppModelDex *dexpp_model_new(uint32_t version);

/** A deep copy of a model. */
DEXPP_C_API DexppModelDex *dexpp_model_clone(const DexppModelDex *dex);

/** Free a model. Accepts NULL. */
DEXPP_C_API void dexpp_model_free(DexppModelDex *dex);

/** Write the model to DEX bytes. `debug_info` nonzero writes debug info. On
 *  success returns a malloc'd buffer of `*out_len` bytes (free with
 *  dexpp_buffer_free()). Errors: INVALID_MODEL (with the problems),
 *  POOL_OVERFLOW, VERSION_TOO_LOW. */
DEXPP_C_API uint8_t *dexpp_model_write(const DexppModelDex *dex, int debug_info, size_t *out_len,
                                       DexppError **err);

/** Write straight to a file. Returns 1 on success, 0 on failure. Errors as
 *  dexpp_model_write(), plus CANNOT_WRITE. */
DEXPP_C_API int dexpp_model_write_file(const DexppModelDex *dex, const char *path, int debug_info,
                                       DexppError **err);

/* ---- Version, pools, validation ----------------------------------------- */

/** The DEX format version dexpp_model_write() uses. */
DEXPP_C_API uint32_t dexpp_model_version(const DexppModelDex *dex);

/** Set the DEX format version. Writing never raises it by itself. */
DEXPP_C_API void dexpp_model_set_version(DexppModelDex *dex, uint32_t version);

/** The lowest DEX version that has every feature the model uses. */
DEXPP_C_API uint32_t dexpp_model_required_version(const DexppModelDex *dex);

/** How many entries each pool of the written file would have. Use it to decide
 *  how to split classes across files: a DEX file holds at most 65535 types and
 *  prototypes, and at most 65536 fields, methods, method handles and call
 *  sites. */
typedef struct DexppPoolStats
{
    size_t strings;        /**< Number of strings. */
    size_t types;          /**< Number of types. */
    size_t protos;         /**< Number of method prototypes. */
    size_t fields;         /**< Number of field identities. */
    size_t methods;        /**< Number of method identities. */
    size_t method_handles; /**< Number of method handles. */
    size_t call_sites;     /**< Number of call sites. */
} DexppPoolStats;

/** The pool sizes the model would write. */
DEXPP_C_API DexppPoolStats dexpp_model_pool_stats(const DexppModelDex *dex);

/** One rule of the format a model breaks. */
typedef struct DexppProblem
{
    char *where;   /**< e.g. "Lcom/a/B;->run()V, item 12"; "" for the whole file. */
    char *message; /**< What is wrong. */
} DexppProblem;

/** Every rule of the format the model breaks. Free with
 *  dexpp_problem_array_free(). An empty array (NULL, `*count` 0) means
 *  dexpp_model_write() can encode the model. */
DEXPP_C_API DexppProblem *dexpp_model_validate(const DexppModelDex *dex, size_t *count);

/** Free an array of problems. Accepts NULL. */
DEXPP_C_API void dexpp_problem_array_free(DexppProblem *problems, size_t count);

/* ---- Classes ------------------------------------------------------------ */

/** The number of classes in the model. */
DEXPP_C_API size_t dexpp_model_class_count(const DexppModelDex *dex);

/** The descriptors of every class, in model order. Free with
 *  dexpp_string_array_free(). */
DEXPP_C_API char **dexpp_model_class_descriptors(const DexppModelDex *dex, size_t *count);

/** 1 when the model has a class with this descriptor, otherwise 0. */
DEXPP_C_API int dexpp_model_has_class(const DexppModelDex *dex, const char *descriptor);

/** Facts about one class of a model. Release with dexpp_class_info_clear(). */
typedef struct DexppClassInfo
{
    char *descriptor;       /**< The class descriptor. */
    uint32_t access_flags;  /**< The access flags (see DexppAccessFlag). */
    char *superclass;       /**< The superclass descriptor, or NULL for java.lang.Object. */
    char *source_file;      /**< The source file name, or NULL. */
    size_t interface_count; /**< Number of interfaces implemented directly. */
    size_t field_count;     /**< Number of fields. */
    size_t method_count;    /**< Number of methods. */
} DexppClassInfo;

/** Fill @p out for the class with this descriptor (release with
 *  dexpp_class_info_clear()). Returns 0 when there is no such class; then
 *  @p out is untouched. */
DEXPP_C_API int dexpp_model_class_info(const DexppModelDex *dex, const char *descriptor,
                                       DexppClassInfo *out);

/** Free the strings of a DexppClassInfo (not the struct) and set them to
 *  NULL. */
DEXPP_C_API void dexpp_class_info_clear(DexppClassInfo *info);

/** The interface descriptors a class implements; empty when the class is
 *  absent or implements none. Free with dexpp_string_array_free(). */
DEXPP_C_API char **dexpp_model_class_interfaces(const DexppModelDex *dex, const char *descriptor,
                                                size_t *count);

/* ---- Editing by whole class --------------------------------------------- */

/** Move the class `descriptor` out of `src` and into `dst`. The class's data is
 * moved, not copied, so this is cheap for a large class. Returns 1 on success;
 * 0 and sets *err (DEXPP_ERROR_MODEL) if `src` has no such class, or `dst`
 * already has one with that descriptor. */
DEXPP_C_API int dexpp_model_move_class(DexppModelDex *dst, DexppModelDex *src,
                                       const char *descriptor, DexppError **err);

/** Copy the class `descriptor` from `src` into `dst`, leaving `src` unchanged.
 * Returns 1 on success; 0 and sets *err as dexpp_model_move_class does. */
DEXPP_C_API int dexpp_model_copy_class(DexppModelDex *dst, const DexppModelDex *src,
                                       const char *descriptor, DexppError **err);

/** Remove the class `descriptor` from the model. Returns 1 if it was removed, 0
 * if there was no such class. */
DEXPP_C_API int dexpp_model_remove_class(DexppModelDex *dex, const char *descriptor);

/* ---- Renaming ----------------------------------------------------------- */

/** Rename classes everywhere they are referenced: class definitions,
 * supertypes, member ids, prototypes, array types, annotations and values,
 * call sites, catch types, debug local types, and the class names inside
 * generic signatures. `from[i]` -> `to[i]` for each of the `count` pairs; a
 * descriptor not listed is left unchanged. String constants are never renamed.
 */
DEXPP_C_API void dexpp_model_rename_types(DexppModelDex *dex, const char *const *from,
                                          const char *const *to, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* DEXPP_MODEL_H */
