/* C ABI for dex::model, dexpp's editable DEX model: read a DEX file into an
 * in-memory model, change it, and write a DEX file again.
 *
 * This is the interop boundary for non-C++ consumers (Go via cgo, etc.). The
 * C++ model is a tree of plain structs (src/model/types.hpp); this shim keeps
 * it behind one opaque handle, DexppModelDex, and works at the granularity of
 * whole classes:
 *
 *   - read a DEX into a model, or make an empty model;
 *   - inspect its classes (descriptor, access flags, superclass, interfaces,
 *     source file, field and method counts);
 *   - build a new model by moving or copying whole classes between models —
 *     this is how a DEX is split into several files, or several merged;
 *   - rename every reference to a class (rename_types);
 *   - validate it, ask the lowest DEX version it needs, count its pools;
 *   - write it back to DEX bytes.
 *
 * Editing inside a method (the instruction stream) is NOT exposed here: a
 * class crosses this boundary whole. That covers splitting, merging and
 * renaming; building new classes instruction by instruction stays in the C++
 * API (src/model/types.hpp).
 *
 * Conventions (as in dexpp.h):
 *   - Fallible functions take a `char **err`: on failure they return NULL (or
 *     0) and set *err to a malloc'd message freed with dexpp_string_free; on
 *     success *err is left NULL. `err` may be NULL if the caller does not want
 *     the message.
 *   - Returned strings are malloc'd copies, freed with dexpp_string_free.
 *     Returned string arrays are freed with dexpp_string_array_free, byte
 *     buffers with dexpp_buffer_free, problem arrays with
 *     dexpp_problem_array_free.
 *   - Class descriptors and names here are the model's strings. The DEX format
 *     allows them to differ from a strict identifier, but a type descriptor
 *     never contains a NUL byte, so plain C strings carry them faithfully.
 *   - dexpp_string_free and dexpp_buffer_free come from dexpp.h; include it
 *     too if you use only this header's allocations.
 */
#ifndef DEXPP_MODEL_H
#define DEXPP_MODEL_H

#include <stddef.h>
#include <stdint.h>

#include "dexpp.h" /* dexpp_string_free, dexpp_string_array_free */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DexppModelDex DexppModelDex;

/* ---- Read / write / lifetime -------------------------------------------- */

/* Read a DEX file into a model. `debug_info` nonzero keeps line numbers and
 * local variables. Returns NULL and sets *err on a malformed or unsupported
 * file. */
DexppModelDex *dexpp_model_read(const uint8_t *bytes, size_t len, int debug_info, char **err);
DexppModelDex *dexpp_model_read_file(const char *path, int debug_info, char **err);

/* An empty model of the given DEX version (35, 37, 38, 39 or 40). */
DexppModelDex *dexpp_model_new(uint32_t version);

/* A deep copy of a model. */
DexppModelDex *dexpp_model_clone(const DexppModelDex *dex);

void dexpp_model_free(DexppModelDex *dex);

/* Write the model to DEX bytes. `debug_info` nonzero writes debug info. On
 * success returns a malloc'd buffer of `*out_len` bytes (free with
 * dexpp_buffer_free) and leaves *err NULL; on failure returns NULL and sets
 * *err (the model is invalid, a pool overflowed, or a feature needs a higher
 * version). */
uint8_t *dexpp_model_write(const DexppModelDex *dex, int debug_info, size_t *out_len, char **err);

/* Write straight to a file. Returns 1 on success, 0 and sets *err on failure. */
int dexpp_model_write_file(const DexppModelDex *dex, const char *path, int debug_info, char **err);

/* Free a byte buffer returned by dexpp_model_write. */
void dexpp_buffer_free(uint8_t *buffer);

/* ---- Version, pools, validation ----------------------------------------- */

uint32_t dexpp_model_version(const DexppModelDex *dex);
void dexpp_model_set_version(DexppModelDex *dex, uint32_t version);

/* The lowest DEX version that has every feature the model uses. */
uint32_t dexpp_model_required_version(const DexppModelDex *dex);

/* How many entries each pool of the written file would have. Use it to decide
 * how to split classes across files (the format indexes types and prototypes
 * with 16 bits, fields and methods with 16 bits). */
typedef struct
{
    size_t strings;
    size_t types;
    size_t protos;
    size_t fields;
    size_t methods;
    size_t method_handles;
    size_t call_sites;
} DexppPoolStats;

DexppPoolStats dexpp_model_pool_stats(const DexppModelDex *dex);

/* One problem validate() found. */
typedef struct
{
    char *where;   /* e.g. "Lcom/a/B;->run()V, item 12"; "" for the whole file */
    char *message; /* what is wrong */
} DexppProblem;

/* Every rule of the format the model breaks. A malloc'd array of `*count`
 * problems (free with dexpp_problem_array_free); `*count` 0 (and NULL) means
 * the model is valid. */
DexppProblem *dexpp_model_validate(const DexppModelDex *dex, size_t *count);
void dexpp_problem_array_free(DexppProblem *problems, size_t count);

/* ---- Classes ------------------------------------------------------------ */

/* The number of classes in the model. */
size_t dexpp_model_class_count(const DexppModelDex *dex);

/* The descriptors of every class, in model order. A malloc'd array of `*count`
 * malloc'd strings (free with dexpp_string_array_free). */
char **dexpp_model_class_descriptors(const DexppModelDex *dex, size_t *count);

/* Does the model have a class with this descriptor? */
int dexpp_model_has_class(const DexppModelDex *dex, const char *descriptor);

/* Facts about one class, filled by dexpp_model_class_info. The strings are
 * malloc'd (superclass and source_file are NULL when the class has none); free
 * each with dexpp_string_free. */
typedef struct
{
    char *descriptor;
    uint32_t access_flags;
    char *superclass;  /* descriptor, or NULL for java.lang.Object */
    char *source_file; /* or NULL */
    size_t interface_count;
    size_t field_count;
    size_t method_count;
} DexppClassInfo;

/* Fill `out` for the class with this descriptor. Returns 1 on success, 0 if no
 * such class (then `out` is untouched). Free the strings in `out` with
 * dexpp_string_free / dexpp_class_info_free. */
int dexpp_model_class_info(const DexppModelDex *dex, const char *descriptor, DexppClassInfo *out);

/* Free the malloc'd strings held in a DexppClassInfo (not the struct itself). */
void dexpp_class_info_free(DexppClassInfo *info);

/* The interface descriptors a class implements. NULL and *count 0 if the class
 * is absent or implements none. Free with dexpp_string_array_free. */
char **dexpp_model_class_interfaces(const DexppModelDex *dex, const char *descriptor,
                                    size_t *count);

/* ---- Editing by whole class --------------------------------------------- */

/* Move the class `descriptor` out of `src` and into `dst`. The class's data is
 * moved, not copied, so this is cheap for a large class. Returns 1 on success;
 * 0 and sets *err if `src` has no such class, or `dst` already has one with
 * that descriptor. */
int dexpp_model_move_class(DexppModelDex *dst, DexppModelDex *src, const char *descriptor,
                           char **err);

/* Copy the class `descriptor` from `src` into `dst`, leaving `src` unchanged.
 * Returns 1 on success; 0 and sets *err as dexpp_model_move_class does. */
int dexpp_model_copy_class(DexppModelDex *dst, const DexppModelDex *src, const char *descriptor,
                           char **err);

/* Remove the class `descriptor` from the model. Returns 1 if it was removed, 0
 * if there was no such class. */
int dexpp_model_remove_class(DexppModelDex *dex, const char *descriptor);

/* ---- Renaming ----------------------------------------------------------- */

/* Rename classes everywhere they are referenced: class definitions,
 * supertypes, member ids, prototypes, array types, annotations and values,
 * call sites, catch types, debug local types, and the class names inside
 * generic signatures. `from[i]` -> `to[i]` for each of the `count` pairs; a
 * descriptor not listed is left unchanged. String constants are never renamed.
 */
void dexpp_model_rename_types(DexppModelDex *dex, const char *const *from, const char *const *to,
                              size_t count);

#ifdef __cplusplus
}
#endif

#endif /* DEXPP_MODEL_H */
