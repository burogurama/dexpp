/**
 * @file dexpp.h
 * @brief C API for code analysis and APK metadata.
 *
 * This is the interop boundary for programs that cannot call C++ directly
 * (Go through cgo, and other languages with a C foreign-function interface).
 * It covers what the Python binding covers: loading, classes, methods and
 * fields, decoded bytecode, control-flow graphs, try blocks, annotations, the
 * call graph, the class hierarchy, cross-references, the manifest, resources,
 * archive entries and signing. The editable model is in dexpp_model.h.
 *
 * Conventions, the same for every function:
 *
 * - **Errors.** A function that can fail takes a `DexppError **err` as its
 *   last argument. On failure it returns NULL (or 0) and, when `err` is not
 *   NULL, sets `*err` to a new error object. On success it sets `*err` to
 *   NULL. So the caller can always pass the result to dexpp_error_free(),
 *   which accepts NULL. `err` may be NULL when the caller does not want the
 *   details.
 * - **Handles** (DexppContext, DexppApk, DexppClass, DexppClassRef,
 *   DexppMethod, DexppField, DexppCallGraph, DexppClassHierarchy, DexppXrefs)
 *   are opaque pointers. Free each one with its `_free` function. Every
 *   handle keeps the analysis it came from alive, so it stays valid after the
 *   context or APK handle it came from is freed. A handle must not be NULL
 *   when it is passed to a function, unless the function says so.
 * - **Strings** are UTF-8, returned as malloc'd copies; free them with
 *   dexpp_string_free(). String *constants* of the analysed program can hold
 *   NUL bytes, so they come back as DexppText, which carries its length.
 * - **Arrays** come with a count in `*count`. An empty array is returned as
 *   NULL with `*count` set to 0. Free an array with the `_array_free`
 *   function named for its type; it frees everything the elements own, and
 *   accepts NULL. An array of handles is one block from malloc(): a caller
 *   that keeps the handles, to free each one later with its `_free`
 *   function, frees the block alone with the array's `_array_free` function
 *   and a count of 0. free() also works when the library is linked
 *   statically, but not across the boundary of a Windows DLL, where the
 *   library and the caller can use different C runtimes.
 * - **Structs filled through an out-pointer** own malloc'd members; release
 *   them with the matching `_clear` function, which frees the members (not
 *   the struct) and sets them to NULL.
 * - **Thread safety.** Reading from one analysis on several threads at once
 *   is safe, as in C++. A single handle must not be freed while another
 *   thread uses it.
 */
#ifndef DEXPP_H
#define DEXPP_H

#include <stddef.h>
#include <stdint.h>

/* DEXPP_C_API marks every function of the C API. When the C API is built as
 * a shared library (the CMake target dexpp_c_shared, which defines
 * DEXPP_C_BUILD_SHARED), it exports the function: __declspec(dllexport) on
 * Windows, default visibility elsewhere, where everything else in the library
 * is hidden. In every other build, and for programs that include this header,
 * it is empty. */
#ifndef DEXPP_C_API
#if defined(DEXPP_C_BUILD_SHARED) && defined(_WIN32)
#define DEXPP_C_API __declspec(dllexport)
#elif defined(DEXPP_C_BUILD_SHARED)
#define DEXPP_C_API __attribute__((visibility("default")))
#else
#define DEXPP_C_API
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================== */
/** @name Errors */
/* ======================================================================== */
/** @{ */

/** What went wrong in a failed call. The names match the Python exceptions. */
typedef enum DexppErrorCode {
    DEXPP_ERROR_FILE_NOT_FOUND = 1,  /**< The path does not exist. */
    DEXPP_ERROR_CANNOT_READ = 2,     /**< The path exists but cannot be read (a directory). */
    DEXPP_ERROR_CANNOT_WRITE = 3,    /**< The file cannot be created or written. */
    DEXPP_ERROR_INVALID_APK = 4,     /**< Not a ZIP archive, or an APK with no classes.dex. */
    DEXPP_ERROR_INVALID_DEX = 5,     /**< A DEX file cannot be parsed. */
    DEXPP_ERROR_UNSUPPORTED_DEX = 6, /**< A DEX format the model does not read (version 41). */
    DEXPP_ERROR_INVALID_MODEL = 7,   /**< A model breaks rules of the format; see the problems. */
    DEXPP_ERROR_POOL_OVERFLOW = 8,   /**< A table of the written file would be too large. */
    DEXPP_ERROR_VERSION_TOO_LOW = 9, /**< A model uses a feature its DEX version lacks. */
    DEXPP_ERROR_MODEL = 10,          /**< A class to move or copy is missing, or already there. */
    DEXPP_ERROR_INTERNAL = 11        /**< An unexpected failure, such as running out of memory. */
} DexppErrorCode;

/** An error reported by a failed call. Free it with dexpp_error_free(). */
typedef struct DexppError DexppError;

/** The kind of the error. */
DEXPP_C_API DexppErrorCode dexpp_error_code(const DexppError *error);

/** The error message. The string belongs to the error and lives until
 *  dexpp_error_free(). */
DEXPP_C_API const char *dexpp_error_message(const DexppError *error);

/** How many problems an error with code DEXPP_ERROR_INVALID_MODEL lists
 *  (0 for every other code). */
DEXPP_C_API size_t dexpp_error_problem_count(const DexppError *error);

/** Where problem @p index is, e.g. `"Lcom/a/B;->run()V, item 12"`; empty for
 *  a problem with the whole file. NULL when @p index is out of range. The
 *  string belongs to the error. */
DEXPP_C_API const char *dexpp_error_problem_where(const DexppError *error, size_t index);

/** What is wrong in problem @p index. NULL when @p index is out of range. The
 *  string belongs to the error. */
DEXPP_C_API const char *dexpp_error_problem_message(const DexppError *error, size_t index);

/** Free an error. Accepts NULL. */
DEXPP_C_API void dexpp_error_free(DexppError *error);

/** @} */

/* ======================================================================== */
/** @name Memory */
/* ======================================================================== */
/** @{ */

/** A string constant of the analysed program. `data` is malloc'd and also
 *  NUL-terminated, but the string itself may contain NUL bytes: `length` is
 *  the number of bytes. */
typedef struct DexppText
{
    char *data;    /**< UTF-8 bytes, followed by a NUL byte. */
    size_t length; /**< Number of bytes, not counting the final NUL. */
} DexppText;

/** Free a string returned by any dexpp function. Accepts NULL. */
DEXPP_C_API void dexpp_string_free(char *s);

/** Free an array of strings and every string in it. Accepts NULL. */
DEXPP_C_API void dexpp_string_array_free(char **strings, size_t count);

/** Free the bytes of one DexppText and set its members to NULL and 0. */
DEXPP_C_API void dexpp_text_clear(DexppText *text);

/** Free an array of DexppText and every text in it. Accepts NULL. */
DEXPP_C_API void dexpp_text_array_free(DexppText *texts, size_t count);

/** Free a byte buffer returned by dexpp_apk_read() or dexpp_model_write().
 *  Accepts NULL. */
DEXPP_C_API void dexpp_buffer_free(uint8_t *buffer);

/** Free an array of indices (node, edge or block numbers). Accepts NULL. */
DEXPP_C_API void dexpp_index_array_free(uint32_t *indices, size_t count);

/** @} */

/* ======================================================================== */
/** @name Enums */
/* ======================================================================== */
/** @{ */

/** Access flags of a class, a method or a field, as the DEX format stores
 *  them. Two pairs share a bit: VOLATILE/BRIDGE and TRANSIENT/VARARGS. */
typedef enum DexppAccessFlag {
    DEXPP_ACC_PUBLIC = 0x0001,
    DEXPP_ACC_PRIVATE = 0x0002,
    DEXPP_ACC_PROTECTED = 0x0004,
    DEXPP_ACC_STATIC = 0x0008,
    DEXPP_ACC_FINAL = 0x0010,
    DEXPP_ACC_SYNCHRONIZED = 0x0020,
    DEXPP_ACC_VOLATILE = 0x0040,  /**< Fields. */
    DEXPP_ACC_BRIDGE = 0x0040,    /**< Methods; the same bit as VOLATILE. */
    DEXPP_ACC_TRANSIENT = 0x0080, /**< Fields. */
    DEXPP_ACC_VARARGS = 0x0080,   /**< Methods; the same bit as TRANSIENT. */
    DEXPP_ACC_NATIVE = 0x0100,
    DEXPP_ACC_INTERFACE = 0x0200,
    DEXPP_ACC_ABSTRACT = 0x0400,
    DEXPP_ACC_STRICT = 0x0800,
    DEXPP_ACC_SYNTHETIC = 0x1000,
    DEXPP_ACC_ANNOTATION = 0x2000,
    DEXPP_ACC_ENUM = 0x4000,
    DEXPP_ACC_CONSTRUCTOR = 0x10000,
    DEXPP_ACC_DECLARED_SYNCHRONIZED = 0x20000
} DexppAccessFlag;

/** The kind of invoke instruction at a call site. */
typedef enum DexppInvokeKind {
    DEXPP_INVOKE_VIRTUAL = 0,
    DEXPP_INVOKE_SUPER = 1,
    DEXPP_INVOKE_DIRECT = 2,
    DEXPP_INVOKE_STATIC = 3,
    DEXPP_INVOKE_INTERFACE = 4,
    DEXPP_INVOKE_POLYMORPHIC = 5,
    DEXPP_INVOKE_CUSTOM = 6
} DexppInvokeKind;

/** How a call-graph edge came to point at its callee. */
typedef enum DexppEdgeOrigin {
    DEXPP_EDGE_DECLARED = 0,            /**< The target the bytecode names. */
    DEXPP_EDGE_INHERITED_RESOLUTION = 1 /**< The nearest definition in a loaded ancestor. */
} DexppEdgeOrigin;

/** What a call-graph node stands for. */
typedef enum DexppCallNodeKind {
    DEXPP_CALL_NODE_DEFINED = 0,  /**< A method defined in a loaded file. */
    DEXPP_CALL_NODE_EXTERNAL = 1, /**< A method that loaded code calls but no loaded file
                                       defines, such as a framework method. */
    DEXPP_CALL_NODE_CALL_SITE = 2 /**< An invoke-custom call site. Its id is opaque: it only
                                       keeps call sites apart and names no method. */
} DexppCallNodeKind;

/** The kind of an edge between two basic blocks. */
typedef enum DexppCfgEdgeKind {
    DEXPP_CFG_FALLTHROUGH = 0,
    DEXPP_CFG_GOTO = 1,
    DEXPP_CFG_BRANCH_TAKEN = 2,
    DEXPP_CFG_BRANCH_NOT_TAKEN = 3,
    DEXPP_CFG_SWITCH_CASE = 4, /**< One case of a switch; the edge's case_key is its key. */
    DEXPP_CFG_SWITCH_DEFAULT = 5,
    DEXPP_CFG_EXCEPTION = 6, /**< To the handler of one exception class; the edge's catch_type
                                  names it. */
    DEXPP_CFG_EXCEPTION_CATCH_ALL = 7 /**< To a handler that catches every exception. */
} DexppCfgEdgeKind;

/** What an instruction does with the type it names. */
typedef enum DexppTypeUseKind {
    DEXPP_TYPE_USE_CONST_CLASS = 0,
    DEXPP_TYPE_USE_CHECK_CAST = 1,
    DEXPP_TYPE_USE_INSTANCE_OF = 2,
    DEXPP_TYPE_USE_NEW_INSTANCE = 3,
    DEXPP_TYPE_USE_NEW_ARRAY = 4,       /**< The descriptor is the array type. */
    DEXPP_TYPE_USE_FILLED_NEW_ARRAY = 5 /**< The descriptor is the array type. */
} DexppTypeUseKind;

/** How many registers a value takes. */
typedef enum DexppOperandWidth {
    DEXPP_WIDTH_SINGLE = 0, /**< 32 bits or a reference: one register. */
    DEXPP_WIDTH_PAIR = 1    /**< long or double: two registers. */
} DexppOperandWidth;

/** Who can see an annotation. */
typedef enum DexppAnnotationVisibility {
    DEXPP_VISIBILITY_BUILD = 0,
    DEXPP_VISIBILITY_RUNTIME = 1,
    DEXPP_VISIBILITY_SYSTEM = 2
} DexppAnnotationVisibility;

/** What a method handle does: the method_handle_type codes of the DEX format.
 *  The four field kinds read or write a field; the five invoke kinds call a
 *  method. */
typedef enum DexppMethodHandleKind {
    DEXPP_METHOD_HANDLE_STATIC_PUT = 0,         /**< Writes a static field. */
    DEXPP_METHOD_HANDLE_STATIC_GET = 1,         /**< Reads a static field. */
    DEXPP_METHOD_HANDLE_INSTANCE_PUT = 2,       /**< Writes an instance field. */
    DEXPP_METHOD_HANDLE_INSTANCE_GET = 3,       /**< Reads an instance field. */
    DEXPP_METHOD_HANDLE_INVOKE_STATIC = 4,      /**< Calls a static method. */
    DEXPP_METHOD_HANDLE_INVOKE_INSTANCE = 5,    /**< Calls an instance method, with dispatch. */
    DEXPP_METHOD_HANDLE_INVOKE_CONSTRUCTOR = 6, /**< Creates an object with a constructor. */
    DEXPP_METHOD_HANDLE_INVOKE_DIRECT = 7,      /**< Calls an instance method without dispatch. */
    DEXPP_METHOD_HANDLE_INVOKE_INTERFACE = 8    /**< Calls an interface method. */
} DexppMethodHandleKind;

/** The kind of a manifest component. */
typedef enum DexppComponentKind {
    DEXPP_COMPONENT_ACTIVITY = 0,
    DEXPP_COMPONENT_SERVICE = 1,
    DEXPP_COMPONENT_RECEIVER = 2,
    DEXPP_COMPONENT_PROVIDER = 3
} DexppComponentKind;

/** @} */

/* ======================================================================== */
/** @name Identities */
/* ======================================================================== */
/** @{ */

/** The identity of a method: its class, name and prototype. The members are
 *  malloc'd strings owned by the struct or array that holds it. */
typedef struct DexppMethodId
{
    char *class_descriptor; /**< e.g. "Ljava/lang/Object;". */
    char *name;             /**< e.g. "toString"; constructors are `<init>`. */
    char *proto;            /**< e.g. "()Ljava/lang/String;". */
} DexppMethodId;

/** The identity of a field: its class, name and type. */
typedef struct DexppFieldId
{
    char *class_descriptor; /**< e.g. "Ljava/lang/System;". */
    char *name;             /**< e.g. "out". */
    char *type;             /**< e.g. "Ljava/io/PrintStream;". */
} DexppFieldId;

/** Free the strings of one DexppMethodId and set them to NULL. */
DEXPP_C_API void dexpp_method_id_clear(DexppMethodId *id);

/** Free the strings of one DexppFieldId and set them to NULL. */
DEXPP_C_API void dexpp_field_id_clear(DexppFieldId *id);

/** Free an array of DexppMethodId. Accepts NULL. */
DEXPP_C_API void dexpp_method_id_array_free(DexppMethodId *ids, size_t count);

/** @} */

/* ======================================================================== */
/** @name Handles */
/* ======================================================================== */
/** @{ */

/** The code of a program: one or more DEX files loaded together. */
typedef struct DexppContext DexppContext;
/** An APK: archive, manifest, resources and signatures. */
typedef struct DexppApk DexppApk;
/** A class defined in a loaded DEX file. */
typedef struct DexppClass DexppClass;
/** A reference to a class by descriptor; the class may not be loaded. */
typedef struct DexppClassRef DexppClassRef;
/** A method defined by a loaded class. */
typedef struct DexppMethod DexppMethod;
/** A field declared by a loaded class. */
typedef struct DexppField DexppField;
/** The whole-program call graph. */
typedef struct DexppCallGraph DexppCallGraph;
/** The whole-program class hierarchy. */
typedef struct DexppClassHierarchy DexppClassHierarchy;
/** The whole-program cross-reference index. */
typedef struct DexppXrefs DexppXrefs;

/** @} */

/* ======================================================================== */
/** @name Annotations and values */
/* ======================================================================== */
/** @{ */

/** The kind of a DexppValue. */
typedef enum DexppValueKind {
    DEXPP_VALUE_NULL = 0,
    DEXPP_VALUE_BOOL = 1,           /**< bool_value. */
    DEXPP_VALUE_INT = 2,            /**< int_value: every integer type, widened. */
    DEXPP_VALUE_FLOAT = 3,          /**< float_value: float and double, widened. */
    DEXPP_VALUE_STRING = 4,         /**< string_value. */
    DEXPP_VALUE_TYPE = 5,           /**< type_descriptor: a class literal. */
    DEXPP_VALUE_ENUM = 6,           /**< field: the enum constant's static field. */
    DEXPP_VALUE_FIELD = 7,          /**< field. */
    DEXPP_VALUE_METHOD = 8,         /**< method. */
    DEXPP_VALUE_METHOD_TYPE = 9,    /**< proto. */
    DEXPP_VALUE_METHOD_HANDLE = 10, /**< method_handle_kind, and field or method. */
    DEXPP_VALUE_ARRAY = 11,         /**< elements and element_count. */
    DEXPP_VALUE_ANNOTATION = 12     /**< annotation: a nested annotation. */
} DexppValueKind;

struct DexppAnnotation;

/** An annotation element value, or the initial value of a static field.
 *  Only the members named by `kind` are set; the others are 0 or NULL. */
typedef struct DexppValue
{
    DexppValueKind kind;    /**< Which members below are meaningful. */
    int bool_value;         /**< BOOL: 1 or 0. */
    int64_t int_value;      /**< INT. */
    double float_value;     /**< FLOAT. */
    DexppText string_value; /**< STRING. */
    char *type_descriptor;  /**< TYPE. */
    char *proto;            /**< METHOD_TYPE. */
    DexppFieldId field;     /**< ENUM, FIELD, and METHOD_HANDLE of a field kind. */
    DexppMethodId method;   /**< METHOD, and METHOD_HANDLE of an invoke kind. */
    /** METHOD_HANDLE: what the handle does. A damaged handle gives INVOKE_STATIC and an
     *  empty method. */
    DexppMethodHandleKind method_handle_kind;
    struct DexppValue *elements;        /**< ARRAY: the elements. */
    size_t element_count;               /**< ARRAY: the number of elements. */
    struct DexppAnnotation *annotation; /**< ANNOTATION: the nested annotation. */
} DexppValue;

/** One `name = value` pair of an annotation. */
typedef struct DexppAnnotationElement
{
    char *name;       /**< The element name. */
    DexppValue value; /**< The element value. */
} DexppAnnotationElement;

/** One annotation on a class, a method or a field. Elements left at their
 *  default value are not listed. */
typedef struct DexppAnnotation
{
    char *type_descriptor;                /**< The annotation class. */
    DexppAnnotationVisibility visibility; /**< Who can see it. */
    DexppAnnotationElement *elements;     /**< In DEX order (by name). */
    size_t element_count;                 /**< Number of elements. */
} DexppAnnotation;

/** Free everything a DexppValue owns and reset it to a NULL value. */
DEXPP_C_API void dexpp_value_clear(DexppValue *value);

/** Free an array of annotations and everything they own. Accepts NULL. */
DEXPP_C_API void dexpp_annotation_array_free(DexppAnnotation *annotations, size_t count);

/** @} */

/* ======================================================================== */
/** @name Loading and the analysis context */
/* ======================================================================== */
/** @{ */

/** Load one DEX file. Errors: FILE_NOT_FOUND, CANNOT_READ, INVALID_DEX. */
DEXPP_C_API DexppContext *dexpp_context_from_dex(const char *path, DexppError **err);

/** Load several DEX files into one context, for example the unpacked files of
 *  a multidex app. Errors as dexpp_context_from_dex(). */
DEXPP_C_API DexppContext *dexpp_context_from_dex_files(const char *const *paths, size_t count,
                                                       DexppError **err);

/** Load every top-level classes*.dex of an APK, in multidex order. `threads`
 *  is the number of threads for decompressing and, later, for building the
 *  call graph and the cross-references: 0 lets the library choose, 1 keeps
 *  all work on the calling thread. Errors: FILE_NOT_FOUND, CANNOT_READ,
 *  INVALID_APK, INVALID_DEX. */
DEXPP_C_API DexppContext *dexpp_context_from_apk(const char *path, uint32_t threads,
                                                 DexppError **err);

/** Free a context. Handles that came from it stay valid. Accepts NULL. */
DEXPP_C_API void dexpp_context_free(DexppContext *ctx);

/** Every class of every loaded DEX file, in file order. Free with
 *  dexpp_class_array_free(). */
DEXPP_C_API DexppClass **dexpp_context_classes(const DexppContext *ctx, size_t *count);

/** The class with this descriptor (e.g. "Lcom/example/Foo;"), or NULL when no
 *  loaded file defines it. */
DEXPP_C_API DexppClass *dexpp_context_find_class(const DexppContext *ctx, const char *descriptor);

/** Every string of every loaded DEX file's string table, including names of
 *  classes and members. Decodes the whole table. Free with
 *  dexpp_text_array_free(). */
DEXPP_C_API DexppText *dexpp_context_strings(const DexppContext *ctx, size_t *count);

/** The call graph of the whole program, built on the first call and kept.
 *  Free the handle with dexpp_call_graph_free(). Errors: INTERNAL. */
DEXPP_C_API DexppCallGraph *dexpp_context_call_graph(const DexppContext *ctx, DexppError **err);

/** The class hierarchy of the whole program, built on the first call and
 *  kept. Free with dexpp_class_hierarchy_free(). Errors: INTERNAL. */
DEXPP_C_API DexppClassHierarchy *dexpp_context_class_hierarchy(const DexppContext *ctx,
                                                               DexppError **err);

/** The cross-reference index of the whole program, built on the first call
 *  and kept. Free with dexpp_xrefs_free(). Errors: INTERNAL. */
DEXPP_C_API DexppXrefs *dexpp_context_xrefs(const DexppContext *ctx, DexppError **err);

/** @} */

/* ======================================================================== */
/** @name Classes and class references */
/* ======================================================================== */
/** @{ */

/** The class descriptor, e.g. "Lcom/example/Foo;". */
DEXPP_C_API char *dexpp_class_descriptor(const DexppClass *cls);

/** The package in Java source form, with dots between its parts, e.g.
 *  "com.example" (the same form as the manifest's package); empty for the
 *  default package. */
DEXPP_C_API char *dexpp_class_package(const DexppClass *cls);

/** The direct superclass, or NULL for java.lang.Object itself. Free with
 *  dexpp_class_ref_free(). */
DEXPP_C_API DexppClassRef *dexpp_class_superclass(const DexppClass *cls);

/** The interfaces the class implements directly, in declaration order. Free
 *  with dexpp_class_ref_array_free(). */
DEXPP_C_API DexppClassRef **dexpp_class_interfaces(const DexppClass *cls, size_t *count);

/** The access flags (see DexppAccessFlag). */
DEXPP_C_API uint32_t dexpp_class_access_flags(const DexppClass *cls);

/** 1 when the class is an interface, otherwise 0. */
DEXPP_C_API int dexpp_class_is_interface(const DexppClass *cls);

/** 1 when the class is abstract, otherwise 0. */
DEXPP_C_API int dexpp_class_is_abstract(const DexppClass *cls);

/** The source file name recorded for the class, or NULL. */
DEXPP_C_API char *dexpp_class_source_file(const DexppClass *cls);

/** The methods the class defines: direct methods first, then virtual ones.
 *  Free with dexpp_method_array_free(). */
DEXPP_C_API DexppMethod **dexpp_class_methods(const DexppClass *cls, size_t *count);

/** The fields the class declares: static fields first, then instance fields.
 *  Free with dexpp_field_array_free(). */
DEXPP_C_API DexppField **dexpp_class_fields(const DexppClass *cls, size_t *count);

/** The annotations on the class. Free with dexpp_annotation_array_free(). */
DEXPP_C_API DexppAnnotation *dexpp_class_annotations(const DexppClass *cls, size_t *count);

/** Free a class handle. Accepts NULL. */
DEXPP_C_API void dexpp_class_free(DexppClass *cls);

/** Free an array of class handles and every handle in it. Accepts NULL.
 *  With @p count 0 it frees the array alone and the handles stay valid. */
DEXPP_C_API void dexpp_class_array_free(DexppClass **classes, size_t count);

/** The descriptor the reference names. */
DEXPP_C_API char *dexpp_class_ref_descriptor(const DexppClassRef *ref);

/** 1 when the class is defined in a loaded DEX file, otherwise 0. */
DEXPP_C_API int dexpp_class_ref_is_resolved(const DexppClassRef *ref);

/** The referenced class, or NULL when no loaded file defines it. */
DEXPP_C_API DexppClass *dexpp_class_ref_resolve(const DexppClassRef *ref);

/** Free a class reference. Accepts NULL. */
DEXPP_C_API void dexpp_class_ref_free(DexppClassRef *ref);

/** Free an array of class references and every reference in it. Accepts
 *  NULL. With @p count 0 it frees the array alone and the references stay
 *  valid. */
DEXPP_C_API void dexpp_class_ref_array_free(DexppClassRef **refs, size_t count);

/** @} */

/* ======================================================================== */
/** @name Methods */
/* ======================================================================== */
/** @{ */

/** An incoming argument and the register it arrives in. */
typedef struct DexppParameterRegister
{
    uint16_t reg; /**< The register; a wide argument also uses reg + 1. */
    char *type;   /**< The argument's type; the declaring class for `this`. */
    int is_this;  /**< 1 for the implicit `this` of an instance method. */
    int is_wide;  /**< 1 for long and double. */
} DexppParameterRegister;

/** One call in a method's code. */
typedef struct DexppMethodCall
{
    uint32_t code_offset; /**< Offset of the invoke instruction, in code units. */
    DexppMethodId target; /**< The called method, as the bytecode names it. */
    DexppInvokeKind kind; /**< The kind of invoke. */
} DexppMethodCall;

/** One field read or write in a method's code. */
typedef struct DexppFieldAccess
{
    uint32_t code_offset; /**< Offset of the instruction, in code units. */
    DexppFieldId field;   /**< The field. */
    int is_write;         /**< 1 for a write. */
    int is_static;        /**< 1 for a static field access. */
} DexppFieldAccess;

/** One const-string instruction in a method's code. */
typedef struct DexppStringLoad
{
    uint32_t code_offset; /**< Offset of the instruction, in code units. */
    DexppText value;      /**< The string that is loaded. */
} DexppStringLoad;

/** One instruction in a method's code that names a type. */
typedef struct DexppTypeUse
{
    uint32_t code_offset;  /**< Offset of the instruction, in code units. */
    char *descriptor;      /**< The type. */
    DexppTypeUseKind kind; /**< What the instruction does with it. */
} DexppTypeUse;

/** One typed catch clause of a try block. */
typedef struct DexppCatchHandler
{
    char *type_descriptor;   /**< The caught exception class. */
    uint32_t handler_offset; /**< Offset of the handler's first instruction. */
} DexppCatchHandler;

/** A range of code covered by exception handlers. */
typedef struct DexppTryBlock
{
    uint32_t start_offset;       /**< First covered code unit. */
    uint32_t end_offset;         /**< One past the last covered code unit. */
    DexppCatchHandler *handlers; /**< Typed catch clauses, in order. */
    size_t handler_count;        /**< Number of handlers. */
    int has_catch_all;           /**< 1 when a catch-all (finally) handler exists. */
    uint32_t catch_all_offset;   /**< Its offset, when has_catch_all is 1. */
} DexppTryBlock;

/** The method name. Constructors are `<init>`, the static initializer is
 *  `<clinit>`. */
DEXPP_C_API char *dexpp_method_name(const DexppMethod *m);

/** The prototype: the parameter types inside parentheses, then the return
 *  type, e.g. "(Ljava/lang/String;I)V". It is the `proto` of the method's
 *  DexppMethodId. */
DEXPP_C_API char *dexpp_method_proto(const DexppMethod *m);

/** The class that defines the method. Free with dexpp_class_ref_free(). */
DEXPP_C_API DexppClassRef *dexpp_method_declaring_class(const DexppMethod *m);

/** Fill @p out with the method's identity: its class, name and prototype
 *  (release with dexpp_method_id_clear()). It is the identity the call graph
 *  and the cross-references use. */
DEXPP_C_API void dexpp_method_id(const DexppMethod *m, DexppMethodId *out);

/** The return type descriptor; "V" for void. */
DEXPP_C_API char *dexpp_method_return_type(const DexppMethod *m);

/** The declared parameter types, in order (`this` is not included). Free
 *  with dexpp_string_array_free(). */
DEXPP_C_API char **dexpp_method_parameters(const DexppMethod *m, size_t *count);

/** The access flags (see DexppAccessFlag). */
DEXPP_C_API uint32_t dexpp_method_access_flags(const DexppMethod *m);

/** 1 when the method has bytecode (it is neither abstract nor native). */
DEXPP_C_API int dexpp_method_has_code(const DexppMethod *m);

/** 1 when the method is static. */
DEXPP_C_API int dexpp_method_is_static(const DexppMethod *m);

/** 1 for `<init>` and `<clinit>`. */
DEXPP_C_API int dexpp_method_is_constructor(const DexppMethod *m);

/** The number of registers in the method's frame; 0 without code. */
DEXPP_C_API uint16_t dexpp_method_register_count(const DexppMethod *m);

/** The incoming arguments and their registers, `this` first for an instance
 *  method. Free with dexpp_parameter_register_array_free(). */
DEXPP_C_API DexppParameterRegister *dexpp_method_parameter_registers(const DexppMethod *m,
                                                                     size_t *count);

/** Every call in the method's code, in order. Free with
 *  dexpp_method_call_array_free(). */
DEXPP_C_API DexppMethodCall *dexpp_method_calls(const DexppMethod *m, size_t *count);

/** Every field read and write in the method's code, in order. Free with
 *  dexpp_field_access_array_free(). */
DEXPP_C_API DexppFieldAccess *dexpp_method_field_accesses(const DexppMethod *m, size_t *count);

/** Every const-string in the method's code, in order. Free with
 *  dexpp_string_load_array_free(). */
DEXPP_C_API DexppStringLoad *dexpp_method_string_loads(const DexppMethod *m, size_t *count);

/** Every instruction that names a type, in order. Free with
 *  dexpp_type_use_array_free(). */
DEXPP_C_API DexppTypeUse *dexpp_method_type_uses(const DexppMethod *m, size_t *count);

/** The try blocks and their handlers. Free with
 *  dexpp_try_block_array_free(). */
DEXPP_C_API DexppTryBlock *dexpp_method_try_blocks(const DexppMethod *m, size_t *count);

/** The annotations on the method. Free with dexpp_annotation_array_free(). */
DEXPP_C_API DexppAnnotation *dexpp_method_annotations(const DexppMethod *m, size_t *count);

/** Free a method handle. Accepts NULL. */
DEXPP_C_API void dexpp_method_free(DexppMethod *m);

/** Free an array of method handles and every handle in it. Accepts NULL.
 *  With @p count 0 it frees the array alone and the handles stay valid. */
DEXPP_C_API void dexpp_method_array_free(DexppMethod **methods, size_t count);

/** Free an array of DexppParameterRegister. Accepts NULL. */
DEXPP_C_API void dexpp_parameter_register_array_free(DexppParameterRegister *regs, size_t count);

/** Free an array of DexppMethodCall. Accepts NULL. */
DEXPP_C_API void dexpp_method_call_array_free(DexppMethodCall *calls, size_t count);

/** Free an array of DexppFieldAccess. Accepts NULL. */
DEXPP_C_API void dexpp_field_access_array_free(DexppFieldAccess *accesses, size_t count);

/** Free an array of DexppStringLoad. Accepts NULL. */
DEXPP_C_API void dexpp_string_load_array_free(DexppStringLoad *loads, size_t count);

/** Free an array of DexppTypeUse. Accepts NULL. */
DEXPP_C_API void dexpp_type_use_array_free(DexppTypeUse *uses, size_t count);

/** Free an array of DexppTryBlock and their handlers. Accepts NULL. */
DEXPP_C_API void dexpp_try_block_array_free(DexppTryBlock *blocks, size_t count);

/** @} */

/* ======================================================================== */
/** @name Fields */
/* ======================================================================== */
/** @{ */

/** The field name. */
DEXPP_C_API char *dexpp_field_name(const DexppField *f);

/** The class that declares the field. Free with dexpp_class_ref_free(). */
DEXPP_C_API DexppClassRef *dexpp_field_declaring_class(const DexppField *f);

/** The field's type descriptor. */
DEXPP_C_API char *dexpp_field_type(const DexppField *f);

/** Fill @p out with the field's identity: its class, name and type (release
 *  with dexpp_field_id_clear()). It is the identity the cross-references
 *  use. */
DEXPP_C_API void dexpp_field_id(const DexppField *f, DexppFieldId *out);

/** The access flags (see DexppAccessFlag). */
DEXPP_C_API uint32_t dexpp_field_access_flags(const DexppField *f);

/** The annotations on the field. Free with dexpp_annotation_array_free(). */
DEXPP_C_API DexppAnnotation *dexpp_field_annotations(const DexppField *f, size_t *count);

/** The constant a static field starts with. Returns 1 and fills @p out
 *  (release with dexpp_value_clear()) when the field has a recorded value;
 *  returns 0 when it has none, which means the type's default (0 or null). */
DEXPP_C_API int dexpp_field_initial_value(const DexppField *f, DexppValue *out);

/** Free a field handle. Accepts NULL. */
DEXPP_C_API void dexpp_field_free(DexppField *f);

/** Free an array of field handles and every handle in it. Accepts NULL.
 *  With @p count 0 it frees the array alone and the handles stay valid. */
DEXPP_C_API void dexpp_field_array_free(DexppField **fields, size_t count);

/** @} */

/* ======================================================================== */
/** @name Instructions */
/* ======================================================================== */
/** @{ */

/** The kind of a decoded instruction: one per instruction family, the same
 *  families as the C++ and Python APIs. */
typedef enum DexppInstructionKind {
    DEXPP_INSN_NOP = 0,                  /**< nop. */
    DEXPP_INSN_GOTO = 1,                 /**< branch_offset. */
    DEXPP_INSN_IF = 2,                   /**< branch_offset, reg_a, reg_b. */
    DEXPP_INSN_IF_ZERO = 3,              /**< branch_offset, reg. */
    DEXPP_INSN_SWITCH = 4,               /**< payload_offset, reg, is_packed, cases. */
    DEXPP_INSN_RETURN = 5,               /**< has_reg, reg, width. */
    DEXPP_INSN_THROW = 6,                /**< reg. */
    DEXPP_INSN_MOVE = 7,                 /**< dest, has_src, src, width. */
    DEXPP_INSN_CONST = 8,                /**< dest, value, width. */
    DEXPP_INSN_CONST_STRING = 9,         /**< dest, string_index. */
    DEXPP_INSN_CONST_CLASS = 10,         /**< dest, type_index. */
    DEXPP_INSN_CONST_METHOD_HANDLE = 11, /**< dest, method_handle_index. */
    DEXPP_INSN_TYPE = 12,                /**< type_index, dest, has_src, src. */
    DEXPP_INSN_FILLED_NEW_ARRAY = 13,    /**< type_index, arg_regs. */
    DEXPP_INSN_MONITOR = 14,             /**< reg, is_enter. */
    DEXPP_INSN_FILL_ARRAY = 15,          /**< reg, payload_offset. */
    DEXPP_INSN_COMPARE = 16,             /**< dest, src_a, src_b, src_width. */
    DEXPP_INSN_ARRAY_OP = 17,            /**< value_reg, array_reg, index_reg, is_write, width. */
    DEXPP_INSN_FIELD = 18,               /**< field_index, value_reg, has_object_reg, object_reg,
                                              is_static, is_write, width. */
    DEXPP_INSN_INVOKE = 19,              /**< method_index, arg_regs. */
    DEXPP_INSN_INVOKE_CUSTOM = 20,       /**< call_site_index, arg_regs. */
    DEXPP_INSN_UNARY_OP = 21,            /**< dest, src, dest_width, src_width. */
    DEXPP_INSN_BINARY_OP = 22,           /**< dest, src_a, src_b, width. */
    DEXPP_INSN_BINARY_LIT = 23,          /**< dest, src, literal. */
    DEXPP_INSN_CONST_METHOD_TYPE = 24,   /**< dest, proto_index. */
    DEXPP_INSN_INVOKE_POLYMORPHIC = 25,  /**< method_index, proto_index, arg_regs. */
    DEXPP_INSN_ARRAY_LENGTH = 26,        /**< dest, array_reg. */
    DEXPP_INSN_INVALID = 27              /**< An opcode the format does not use; only offset,
                                              opcode (the raw byte) and size (1). */
} DexppInstructionKind;

/** One case of a switch instruction. */
typedef struct DexppSwitchCase
{
    int32_t key;    /**< The value the case matches. */
    int32_t target; /**< Where it jumps, relative to the switch instruction. */
} DexppSwitchCase;

/** One decoded instruction. `offset`, `opcode` and `size` are set for every
 *  kind; the other members only for the kinds DexppInstructionKind lists
 *  for them, and are 0 or NULL otherwise. Offsets are in 16-bit code units.
 *  Pool indices (string_index, type_index, ...) are raw indices into the DEX
 *  file's tables; dexpp_method_calls() and the other reference functions
 *  give resolved names, and dexpp_method_disassemble() the whole instruction
 *  as text with names. */
typedef struct DexppInstruction
{
    DexppInstructionKind kind;    /**< The instruction family. */
    uint32_t offset;              /**< Position in the method's code. */
    uint8_t opcode;               /**< The raw opcode byte. */
    uint8_t size;                 /**< Length in code units (1 to 5). */
    int32_t branch_offset;        /**< Relative to the instruction's own offset. */
    int32_t payload_offset;       /**< Where the switch or array table is. */
    uint8_t reg;                  /**< The single register operand. */
    int has_reg;                  /**< RETURN: 0 for return-void. */
    uint8_t reg_a;                /**< IF: first register compared. */
    uint8_t reg_b;                /**< IF: second register compared. */
    uint16_t dest;                /**< The register written. */
    uint16_t src;                 /**< The register read. */
    int has_src;                  /**< MOVE: 0 for move-result and move-exception. TYPE: 0 for
                                       check-cast and new-instance. */
    uint8_t src_a;                /**< COMPARE, BINARY_OP: first operand. */
    uint8_t src_b;                /**< COMPARE, BINARY_OP: second operand. */
    int64_t value;                /**< CONST: the constant (bit pattern for floats). */
    int32_t literal;              /**< BINARY_LIT: the constant operand. */
    uint32_t string_index;        /**< CONST_STRING. */
    uint32_t type_index;          /**< CONST_CLASS, TYPE, FILLED_NEW_ARRAY. */
    uint32_t method_handle_index; /**< CONST_METHOD_HANDLE: index in the method handle table. */
    uint32_t proto_index;         /**< CONST_METHOD_TYPE, INVOKE_POLYMORPHIC: prototype index. */
    uint32_t field_index;         /**< FIELD. */
    uint32_t method_index;        /**< INVOKE, INVOKE_POLYMORPHIC. */
    uint32_t call_site_index;     /**< INVOKE_CUSTOM. */
    uint8_t value_reg;            /**< ARRAY_OP, FIELD: the value's register. */
    uint8_t array_reg;            /**< ARRAY_OP, ARRAY_LENGTH. */
    uint8_t index_reg;            /**< ARRAY_OP. */
    uint8_t object_reg;           /**< FIELD: the object's register. */
    int has_object_reg;           /**< FIELD: 0 for a static field. */
    int is_packed;                /**< SWITCH: 1 for packed-switch. */
    int is_enter;                 /**< MONITOR: 1 for monitor-enter. */
    int is_write;                 /**< ARRAY_OP, FIELD: 1 for a write. */
    int is_static;                /**< FIELD: 1 for a static field. */
    DexppOperandWidth width;      /**< RETURN, MOVE, CONST, ARRAY_OP, FIELD, BINARY_OP. */
    DexppOperandWidth src_width;  /**< COMPARE, UNARY_OP. */
    DexppOperandWidth dest_width; /**< UNARY_OP. */
    DexppSwitchCase *cases;       /**< SWITCH: the decoded case table. */
    size_t case_count;            /**< SWITCH: number of cases. */
    uint16_t *arg_regs; /**< FILLED_NEW_ARRAY, INVOKE, INVOKE_POLYMORPHIC, INVOKE_CUSTOM. */
    size_t arg_count;   /**< Number of argument registers. */
} DexppInstruction;

/** The decoded bytecode of a method, in order. Free with
 *  dexpp_instruction_array_free(). */
DEXPP_C_API DexppInstruction *dexpp_method_instructions(const DexppMethod *m, size_t *count);

/** Free an array of instructions. Accepts NULL. */
DEXPP_C_API void dexpp_instruction_array_free(DexppInstruction *insns, size_t count);

/** The name of a Dalvik opcode byte, such as "invoke-virtual". An unused
 *  opcode gives "unused-XX". The string is static: do not free it. */
DEXPP_C_API const char *dexpp_opcode_name(uint8_t opcode);

/** One instruction as text: the opcode name, then its operands, with pool
 *  operands as raw indices (`string@12`). A `kind` this header does not list
 *  gives the opcode name alone, and a width other than DEXPP_WIDTH_PAIR counts
 *  as DEXPP_WIDTH_SINGLE. dexpp_method_disassemble() names the references. */
DEXPP_C_API char *dexpp_instruction_to_string(const DexppInstruction *insn);

/** One instruction of a method as readable text. */
typedef struct DexppDisassemblyLine
{
    uint32_t offset; /**< Offset of the instruction in the method's code, in code units: the
                          same number as the instruction's `offset`. */
    char *text;      /**< The instruction with every reference named, in the style of smali
                          and dexdump. It holds no NUL byte: control characters are
                          escaped. */
} DexppDisassemblyLine;

/** The method's code as readable text: one line per instruction, in the
 *  order of dexpp_method_instructions(), each with the instruction's offset.
 *  Empty for a method without code. Free with
 *  dexpp_disassembly_line_array_free().
 *
 *  The text is the opcode name, then the operands, with every reference named
 *  instead of numbered: a method is `Lclass;->name(Params)Return`, a field
 *  `Lclass;->name:Type`, a type its descriptor, a prototype `(Params)Return`,
 *  and a method handle its kind and member (`invoke-static@LA;->f()V`). A
 *  string is in double quotes, with quotes, backslashes, control characters
 *  and invisible characters escaped. A call site is `call_site@N` followed by
 *  its name, method type and extra arguments in parentheses, then `@` and the
 *  bootstrap method. Literals are hexadecimal as in smali (`0x1f`, `-0x1`,
 *  `0x10L` for a wide constant). Branch targets are code offsets in
 *  hexadecimal, the same numbers as the `offset` of the lines
 *  (`if-eqz v0, 0012`). Switch and array tables are written out in the
 *  instruction that uses them: `packed-switch v0, {0x1 -> 0012}` and
 *  `fill-array-data v0, [0x1, 0x2]`. A reference whose index is outside its
 *  table is the raw index followed by a question mark (`string@4?`), and so
 *  is a branch target outside the code or a damaged table. */
DEXPP_C_API DexppDisassemblyLine *dexpp_method_disassemble(const DexppMethod *m, size_t *count);

/** Free an array of DexppDisassemblyLine and the text of every line. Accepts
 *  NULL. */
DEXPP_C_API void dexpp_disassembly_line_array_free(DexppDisassemblyLine *lines, size_t count);

/** @} */

/* ======================================================================== */
/** @name Control-flow graph */
/* ======================================================================== */
/** @{ */

/** An edge from one basic block to another. */
typedef struct DexppCfgEdge
{
    uint32_t target_block; /**< Index of the block it leads to. */
    DexppCfgEdgeKind kind; /**< Why execution can go along it. */
    int32_t case_key;      /**< SWITCH_CASE: the case key; 0 for every other kind. */
    char *catch_type;      /**< EXCEPTION: descriptor of the caught exception class; NULL for
                                every other kind. */
} DexppCfgEdge;

/** A run of instructions entered at its first and left at its last one. */
typedef struct DexppBasicBlock
{
    uint32_t index;           /**< Position of the block in the array. */
    uint32_t start_offset;    /**< Offset of the first instruction. */
    uint32_t end_offset;      /**< Offset just past the last instruction. */
    uint32_t first_insn;      /**< Index of the first instruction in the method's list. */
    uint32_t insn_count;      /**< Number of instructions. */
    DexppCfgEdge *successors; /**< The edges that leave the block. */
    size_t successor_count;   /**< Number of successors. */
    uint32_t *predecessors;   /**< Indices of the blocks with an edge to this one. */
    size_t predecessor_count; /**< Number of predecessors. */
    int is_handler_entry;     /**< 1 for the first block of a catch handler. */
} DexppBasicBlock;

/** The method's control-flow graph: blocks ordered by offset, block 0 is the
 *  entry. Empty for a method without code. Free with
 *  dexpp_basic_block_array_free(). */
DEXPP_C_API DexppBasicBlock *dexpp_method_cfg(const DexppMethod *m, size_t *count);

/** Free an array of basic blocks and the edges they own. Accepts NULL. */
DEXPP_C_API void dexpp_basic_block_array_free(DexppBasicBlock *blocks, size_t count);

/** @} */

/* ======================================================================== */
/** @name Call graph */
/* ======================================================================== */
/** @{ */

/** A node of the call graph: a method, or an invoke-custom call site. */
typedef struct DexppCallNode
{
    uint32_t index;         /**< Position of the node in the graph. */
    DexppMethodId id;       /**< The method's identity; opaque for a CALL_SITE node. */
    DexppCallNodeKind kind; /**< What the node stands for. */
} DexppCallNode;

/** One call-site edge. A call site has one DECLARED edge, and an
 *  INHERITED_RESOLUTION edge when the named class does not define the
 *  method itself. */
typedef struct DexppCallEdge
{
    uint32_t caller_node;   /**< Index of the calling method's node. */
    uint32_t callee_node;   /**< Index of the called method's node. */
    DexppInvokeKind kind;   /**< The kind of invoke. */
    uint32_t code_offset;   /**< Offset of the invoke in the caller's code. */
    DexppEdgeOrigin origin; /**< Declared target or inherited resolution. */
} DexppCallEdge;

/** Number of nodes. */
DEXPP_C_API size_t dexpp_call_graph_node_count(const DexppCallGraph *g);

/** Number of edges. */
DEXPP_C_API size_t dexpp_call_graph_edge_count(const DexppCallGraph *g);

/** Every node, in index order. Free with dexpp_call_node_array_free(). */
DEXPP_C_API DexppCallNode *dexpp_call_graph_nodes(const DexppCallGraph *g, size_t *count);

/** Fill @p out with node @p node (release with dexpp_call_node_clear()).
 *  Returns 0 when @p node is out of range. */
DEXPP_C_API int dexpp_call_graph_node(const DexppCallGraph *g, uint32_t node, DexppCallNode *out);

/** Every edge, in index order. Free with dexpp_call_edge_array_free(). */
DEXPP_C_API DexppCallEdge *dexpp_call_graph_edges(const DexppCallGraph *g, size_t *count);

/** Fill @p out with edge @p edge. Returns 0 when it is out of range. */
DEXPP_C_API int dexpp_call_graph_edge(const DexppCallGraph *g, uint32_t edge, DexppCallEdge *out);

/** The index of the node with this identity, or -1 when the graph has none. */
DEXPP_C_API int64_t dexpp_call_graph_find(const DexppCallGraph *g, const char *class_descriptor,
                                          const char *name, const char *proto);

/** Indices of the edges that leave node @p node (its call sites). Free with
 *  dexpp_index_array_free(). Empty when @p node is out of range. */
DEXPP_C_API uint32_t *dexpp_call_graph_out_edges(const DexppCallGraph *g, uint32_t node,
                                                 size_t *count);

/** Indices of the edges that enter node @p node (the calls into it). */
DEXPP_C_API uint32_t *dexpp_call_graph_in_edges(const DexppCallGraph *g, uint32_t node,
                                                size_t *count);

/** Nodes a virtual or interface call declared against @p node may dispatch
 *  to, besides itself and its own resolution. */
DEXPP_C_API uint32_t *dexpp_call_graph_dispatch_targets(const DexppCallGraph *g, uint32_t node,
                                                        size_t *count);

/** The reverse of dexpp_call_graph_dispatch_targets(). */
DEXPP_C_API uint32_t *dexpp_call_graph_dispatch_sources(const DexppCallGraph *g, uint32_t node,
                                                        size_t *count);

/** Every node @p node may call, dispatch targets included; no duplicates. */
DEXPP_C_API uint32_t *dexpp_call_graph_callees(const DexppCallGraph *g, uint32_t node,
                                               size_t *count);

/** Every node that may call @p node, through dispatch included. */
DEXPP_C_API uint32_t *dexpp_call_graph_callers(const DexppCallGraph *g, uint32_t node,
                                               size_t *count);

/** The dispatch targets that apply at edge @p edge: those of its callee for a
 *  DECLARED virtual or interface edge, none for every other edge. */
DEXPP_C_API uint32_t *dexpp_call_graph_edge_dispatch_targets(const DexppCallGraph *g, uint32_t edge,
                                                             size_t *count);

/** Indices of the edges that match both filters. `origin` is a
 *  DexppEdgeOrigin or -1 for any; `kind` is a DexppInvokeKind or -1 for any.
 *  The filtering runs in C++. */
DEXPP_C_API uint32_t *dexpp_call_graph_edges_where(const DexppCallGraph *g, int origin, int kind,
                                                   size_t *count);

/** The method behind a DEFINED node, or NULL for an EXTERNAL or CALL_SITE
 *  node or an index out of range. Free with dexpp_method_free(). */
DEXPP_C_API DexppMethod *dexpp_call_graph_method_for(const DexppCallGraph *g, uint32_t node);

/** Free a call-graph handle. Accepts NULL. */
DEXPP_C_API void dexpp_call_graph_free(DexppCallGraph *g);

/** Free the strings of one DexppCallNode. */
DEXPP_C_API void dexpp_call_node_clear(DexppCallNode *node);

/** Free an array of DexppCallNode. Accepts NULL. */
DEXPP_C_API void dexpp_call_node_array_free(DexppCallNode *nodes, size_t count);

/** Free an array of DexppCallEdge. Accepts NULL. */
DEXPP_C_API void dexpp_call_edge_array_free(DexppCallEdge *edges, size_t count);

/** @} */

/* ======================================================================== */
/** @name Class hierarchy */
/* ======================================================================== */
/** @{ */

/** The direct superclass, then the directly implemented interfaces. Free
 *  string arrays with dexpp_string_array_free(). */
DEXPP_C_API char **dexpp_class_hierarchy_supertypes(const DexppClassHierarchy *h,
                                                    const char *descriptor, size_t *count);

/** The direct superclass, or NULL when none is recorded (java.lang.Object, a
 *  class outside the loaded files, or a descriptor the hierarchy does not
 *  know). */
DEXPP_C_API char *dexpp_class_hierarchy_superclass_of(const DexppClassHierarchy *h,
                                                      const char *descriptor);

/** The interfaces implemented (or, for an interface, extended) directly. */
DEXPP_C_API char **dexpp_class_hierarchy_direct_interfaces(const DexppClassHierarchy *h,
                                                           const char *descriptor, size_t *count);

/** The classes that directly extend the class. */
DEXPP_C_API char **dexpp_class_hierarchy_subclasses(const DexppClassHierarchy *h,
                                                    const char *descriptor, size_t *count);

/** The classes that directly implement the interface. */
DEXPP_C_API char **dexpp_class_hierarchy_implementers(const DexppClassHierarchy *h,
                                                      const char *descriptor, size_t *count);

/** Every ancestor class and interface; the class itself is not included. */
DEXPP_C_API char **dexpp_class_hierarchy_all_supertypes(const DexppClassHierarchy *h,
                                                        const char *descriptor, size_t *count);

/** Everything below the class, through extends and implements; the class
 *  itself is not included. */
DEXPP_C_API char **dexpp_class_hierarchy_all_descendants(const DexppClassHierarchy *h,
                                                         const char *descriptor, size_t *count);

/** 1 when @p sub is @p super or has it among its ancestors. */
DEXPP_C_API int dexpp_class_hierarchy_is_subtype_of(const DexppClassHierarchy *h, const char *sub,
                                                    const char *super);

/** 1 when the class is defined in a loaded file. */
DEXPP_C_API int dexpp_class_hierarchy_is_loaded(const DexppClassHierarchy *h,
                                                const char *descriptor);

/** 1 when the class is loaded and is an interface. */
DEXPP_C_API int dexpp_class_hierarchy_is_interface(const DexppClassHierarchy *h,
                                                   const char *descriptor);

/** 1 when the hierarchy has no classes. */
DEXPP_C_API int dexpp_class_hierarchy_empty(const DexppClassHierarchy *h);

/** Free a class-hierarchy handle. Accepts NULL. */
DEXPP_C_API void dexpp_class_hierarchy_free(DexppClassHierarchy *h);

/** @} */

/* ======================================================================== */
/** @name Cross-references */
/* ======================================================================== */
/** @{ */

/** One invoke instruction that calls a method. `referrer` is an index into
 *  dexpp_xrefs_referrers(). */
typedef struct DexppMethodXref
{
    uint32_t referrer;    /**< The method that contains the instruction. */
    uint32_t code_offset; /**< Offset of the instruction, in code units. */
    DexppInvokeKind kind; /**< The kind of invoke. */
} DexppMethodXref;

/** One instruction that reads or writes a field. */
typedef struct DexppFieldXref
{
    uint32_t referrer;    /**< The method that contains the instruction. */
    uint32_t code_offset; /**< Offset of the instruction, in code units. */
    int is_write;         /**< 1 for a write. */
    int is_static;        /**< 1 for a static field access. */
} DexppFieldXref;

/** One const-string instruction. */
typedef struct DexppStringXref
{
    uint32_t referrer;    /**< The method that contains the instruction. */
    uint32_t code_offset; /**< Offset of the instruction, in code units. */
} DexppStringXref;

/** One instruction that names a type. */
typedef struct DexppTypeXref
{
    uint32_t referrer;     /**< The method that contains the instruction. */
    uint32_t code_offset;  /**< Offset of the instruction, in code units. */
    DexppTypeUseKind kind; /**< What the instruction does with the type. */
} DexppTypeXref;

/** The call sites of one method, as found by name. */
typedef struct DexppMethodRefGroup
{
    DexppMethodId target;   /**< The called method. */
    DexppMethodXref *sites; /**< Its call sites. */
    size_t site_count;      /**< Number of call sites. */
} DexppMethodRefGroup;

/** Number of referrers: methods that contain an indexed instruction. */
DEXPP_C_API size_t dexpp_xrefs_referrer_count(const DexppXrefs *x);

/** The identity of every referrer, in index order. Free with
 *  dexpp_method_id_array_free(). */
DEXPP_C_API DexppMethodId *dexpp_xrefs_referrers(const DexppXrefs *x, size_t *count);

/** Fill @p out with the identity of one referrer (release with
 *  dexpp_method_id_clear()). Returns 0 when @p referrer is out of range. */
DEXPP_C_API int dexpp_xrefs_referrer_id(const DexppXrefs *x, uint32_t referrer, DexppMethodId *out);

/** The method of one referrer, or NULL when @p referrer is out of range.
 *  Free with dexpp_method_free(). */
DEXPP_C_API DexppMethod *dexpp_xrefs_referrer_method(const DexppXrefs *x, uint32_t referrer);

/** The call sites whose target, as the bytecode names it, is exactly this
 *  method. Free with dexpp_method_xref_array_free(). */
DEXPP_C_API DexppMethodXref *dexpp_xrefs_method_refs(const DexppXrefs *x,
                                                     const char *class_descriptor, const char *name,
                                                     const char *proto, size_t *count);

/** The call sites of every method with this name, in any class and with any
 *  prototype, grouped by called method. Free with
 *  dexpp_method_ref_group_array_free(). */
DEXPP_C_API DexppMethodRefGroup *dexpp_xrefs_method_refs_by_name(const DexppXrefs *x,
                                                                 const char *name, size_t *count);

/** Every read and write of this field. Free with
 *  dexpp_field_xref_array_free(). */
DEXPP_C_API DexppFieldXref *dexpp_xrefs_field_refs(const DexppXrefs *x,
                                                   const char *class_descriptor, const char *name,
                                                   const char *type, size_t *count);

/** Only the reads of this field. */
DEXPP_C_API DexppFieldXref *dexpp_xrefs_field_reads(const DexppXrefs *x,
                                                    const char *class_descriptor, const char *name,
                                                    const char *type, size_t *count);

/** Only the writes of this field. */
DEXPP_C_API DexppFieldXref *dexpp_xrefs_field_writes(const DexppXrefs *x,
                                                     const char *class_descriptor, const char *name,
                                                     const char *type, size_t *count);

/** The const-string instructions that load exactly this string, given as
 *  @p length bytes (it may contain NUL bytes). Free with
 *  dexpp_string_xref_array_free(). */
DEXPP_C_API DexppStringXref *dexpp_xrefs_string_refs(const DexppXrefs *x, const char *value,
                                                     size_t length, size_t *count);

/** Every distinct string that some const-string instruction loads. Free with
 *  dexpp_text_array_free(). */
DEXPP_C_API DexppText *dexpp_xrefs_referenced_strings(const DexppXrefs *x, size_t *count);

/** The instructions that name this type. Free with
 *  dexpp_type_xref_array_free(). */
DEXPP_C_API DexppTypeXref *dexpp_xrefs_type_refs(const DexppXrefs *x, const char *descriptor,
                                                 size_t *count);

/** 1 when nothing is indexed. */
DEXPP_C_API int dexpp_xrefs_empty(const DexppXrefs *x);

/** Free a cross-reference handle. Accepts NULL. */
DEXPP_C_API void dexpp_xrefs_free(DexppXrefs *x);

/** Free an array of DexppMethodXref. Accepts NULL. */
DEXPP_C_API void dexpp_method_xref_array_free(DexppMethodXref *xrefs, size_t count);

/** Free an array of DexppMethodRefGroup and their sites. Accepts NULL. */
DEXPP_C_API void dexpp_method_ref_group_array_free(DexppMethodRefGroup *groups, size_t count);

/** Free an array of DexppFieldXref. Accepts NULL. */
DEXPP_C_API void dexpp_field_xref_array_free(DexppFieldXref *xrefs, size_t count);

/** Free an array of DexppStringXref. Accepts NULL. */
DEXPP_C_API void dexpp_string_xref_array_free(DexppStringXref *xrefs, size_t count);

/** Free an array of DexppTypeXref. Accepts NULL. */
DEXPP_C_API void dexpp_type_xref_array_free(DexppTypeXref *xrefs, size_t count);

/** @} */

/* ======================================================================== */
/** @name APK, manifest, resources and signing */
/* ======================================================================== */
/** @{ */

/** An `<intent-filter>` of a component. */
typedef struct DexppIntentFilter
{
    char **actions;        /**< The android:name of each `<action>`. */
    size_t action_count;   /**< Number of actions. */
    char **categories;     /**< The android:name of each `<category>`. */
    size_t category_count; /**< Number of categories. */
    int is_launcher;       /**< 1 for action MAIN with category LAUNCHER. */
} DexppIntentFilter;

/** An activity, service, receiver or provider declared in the manifest. */
typedef struct DexppComponent
{
    char *name;              /**< Full class name, e.g. "com.example.Main". Empty when
                                  android:name is missing, or is a reference that gives no
                                  single value. */
    uint32_t name_reference; /**< When android:name is a resource reference: its id,
                                  resolved or not; else 0. */
    DexppComponentKind kind; /**< The kind of component. */
    int exported; /**< android:exported: 1 or 0 as written, or as the resource it refers to
                       says in the default configuration; -1 when absent or when a
                       reference could not be resolved. Text reads as Android reads it:
                       only "1", "true" and "TRUE" are true. */
    uint32_t exported_reference;         /**< When android:exported is a resource reference
                                              ("@bool/name") or a theme attribute ("?attr/name"):
                                              its id, resolved or not; else 0. A nonzero id with
                                              exported == -1 means the value is unknown. */
    int exported_varies;                 /**< 1 when android:exported is a reference whose value is
                                              not the same in every resource configuration (for
                                              example false in res/values, true in
                                              res/values-v31); `exported` holds the default
                                              configuration's value. */
    char *permission;                    /**< The permission a caller needs, or NULL. Also NULL when
                                              android:permission is a reference that gives no single
                                              value: it cannot be resolved, or its string changes with
                                              the configuration, which Android ignores. */
    uint32_t permission_reference;       /**< When android:permission is a resource
                                              reference: its id, resolved or not; else 0. */
    DexppIntentFilter *intent_filters;   /**< The intent filters. */
    size_t intent_filter_count;          /**< Number of intent filters. */
    int guarded_by_signature_permission; /**< 1 when guarded by an app-declared signature
                                              permission, signature in every
                                              configuration. */
    int is_exported;  /**< The effective answer: explicit, or inferred from filters; 1 when
                           the value varies between configurations or is an unresolved
                           reference (possibly exported). */
    int is_reachable; /**< Exported and not guarded by a signature permission. */
} DexppComponent;

/** A permission the app declares. */
typedef struct DexppDeclaredPermission
{
    char *name;                  /**< The permission name. */
    int has_protection_level;    /**< 1 when android:protectionLevel is known: 0 when it is a
                                      resource reference that could not be resolved. */
    int32_t protection_level;    /**< The raw value, in the default configuration for a
                                      reference; the low 4 bits are the base level. A
                                      declaration without the attribute is normal (0). */
    int protection_level_varies; /**< 1 when the level is a reference whose value is not the
                                      same in every configuration. The permission then
                                      guards a component only when every configuration gives
                                      signature or signatureOrSystem. */
} DexppDeclaredPermission;

/** The decoded AndroidManifest.xml. Release with dexpp_manifest_clear().
 *
 *  An attribute may be written as a resource reference, such as
 *  android:debuggable="@bool/debug". The value is then the one the APK's
 *  resource table gives, in its default configuration, and the matching
 *  `*_reference` member holds the reference id. When the reference cannot be
 *  resolved, the value reads as absent (has_* is 0, -1 or NULL) while the
 *  `*_reference` member still holds the id: the attribute is set, but its
 *  value is unknown. A `*_reference` member is 0 for a plain value or a
 *  missing attribute.
 *
 *  A boolean whose resource has another value in some configuration has a
 *  `*_varies` member set to 1. Integers hold the default configuration's
 *  value. A text value is NULL when its string changes with the
 *  configuration. */
typedef struct DexppManifest
{
    char *package;                   /**< The package attribute of `<manifest>`. */
    int has_version_code;            /**< 1 when android:versionCode is set and known. */
    int64_t version_code;            /**< android:versionCode. */
    uint32_t version_code_reference; /**< The reference id of android:versionCode, or 0. */
    char *version_name;              /**< android:versionName, or NULL (also when it is a
                                          reference without a single value). */
    uint32_t version_name_reference; /**< The reference id of android:versionName, or 0. */
    int has_min_sdk;                 /**< 1 when android:minSdkVersion is set and known. */
    int64_t min_sdk;                 /**< android:minSdkVersion. */
    uint32_t min_sdk_reference;      /**< The reference id of android:minSdkVersion, or 0. */
    int has_target_sdk;              /**< 1 when android:targetSdkVersion is set and known. */
    int64_t target_sdk;              /**< android:targetSdkVersion. */
    uint32_t target_sdk_reference;   /**< The reference id of android:targetSdkVersion, or 0. */
    char **permissions;              /**< The name of each `<uses-permission>`. */
    size_t permission_count;         /**< Number of permissions. */
    DexppDeclaredPermission *declared_permissions; /**< Each `<permission>` the app declares. */
    size_t declared_permission_count;              /**< Number of declared permissions. */
    DexppComponent *components;                    /**< Every component, in manifest order. */
    size_t component_count;                        /**< Number of components. */
    char *launcher_activity;                       /**< The launcher activity's name, or NULL. */
    int debuggable;                /**< android:debuggable: 1, 0, or -1 when absent or unknown. */
    uint32_t debuggable_reference; /**< The reference id of android:debuggable, or 0. */
    int debuggable_varies;         /**< 1 when android:debuggable is a reference whose value is
                                        not the same in every configuration. */
    int allow_backup;              /**< android:allowBackup: 1, 0, or -1 when absent or unknown. */
    uint32_t allow_backup_reference; /**< The reference id of android:allowBackup, or 0. */
    int allow_backup_varies;         /**< 1 when android:allowBackup is a reference whose value
                                          is not the same in every configuration. */
} DexppManifest;

/** The full name of a resource: package:type/entry. Release with
 *  dexpp_resource_name_clear(). */
typedef struct DexppResourceName
{
    char *package; /**< e.g. "com.example.app". */
    char *type;    /**< e.g. "string". */
    char *entry;   /**< e.g. "app_name". */
} DexppResourceName;

/** The value of a resource in one configuration, as the table stores it (see
 *  dexpp_apk_resource_values()). A value that refers to another resource is
 *  not followed. */
typedef struct DexppResourceValue
{
    char *configuration; /**< The configuration, written like the qualifiers of a res/ folder
                              name: "" for the default configuration, "v31" for Android 12
                              and newer, "fr" for French, "b+sr+Latn-land-night-v8" for a
                              combination; the text `aapt2 dump resources` prints. */
    uint8_t type;        /**< The Res_value data type from Android's ResourceTypes.h: for
                              example 0x01 a reference, 0x03 a string, 0x10 a decimal integer,
                              0x12 a boolean. */
    uint32_t data;       /**< The raw data: the id a reference names, the integer itself, 0 or
                              not 0 for a boolean, the string's index in the table's pool. */
    DexppText string;    /**< The text of a string value (type 0x03); for any other type,
                              `data` is NULL and `length` 0. */
    int boolean;         /**< The value read as Android reads a boolean: 1 or 0 for an
                              integer-typed value (true when not 0), for exactly "1", "true" or
                              "TRUE" (1) or other text (0), and for a float, a dimension or a
                              fraction (0); -1 for a reference, a null value or empty text. */
    int has_integer;     /**< 1 for an integer-typed value (an integer, a boolean, a color). */
    int64_t integer;     /**< That value as its signed 32-bit data, when has_integer is 1. */
} DexppResourceValue;

/** An X.509 certificate that signed the APK. */
typedef struct DexppCertificate
{
    uint8_t *der;      /**< The certificate in DER form. */
    size_t der_length; /**< Number of DER bytes. */
    char *subject;     /**< e.g. "C=US, O=Example, CN=App". */
    char *issuer;      /**< The issuer, in the same form. */
    char *serial_hex;  /**< The serial number, lowercase hex. */
    char *not_before;  /**< Start of validity, "YYYY-MM-DDTHH:MM:SSZ". */
    char *not_after;   /**< End of validity, same form. */
    char *sha1_hex;    /**< SHA-1 fingerprint, lowercase hex. */
    char *sha256_hex;  /**< SHA-256 fingerprint, lowercase hex. */
    int self_signed;   /**< 1 when subject and issuer are the same. */
} DexppCertificate;

/** Which signature schemes the APK carries, and their certificates. The
 *  signatures are not verified. Release with dexpp_signing_info_clear(). */
typedef struct DexppSigningInfo
{
    int v1;                         /**< 1 for a JAR signature. */
    int v2;                         /**< 1 for an APK Signature Scheme v2 block. */
    int v3;                         /**< 1 for a v3 or v3.1 block. */
    DexppCertificate *certificates; /**< Certificates of every scheme, each listed once. */
    size_t certificate_count;       /**< Number of certificates. */
} DexppSigningInfo;

/** Open an APK file. Errors: FILE_NOT_FOUND, CANNOT_READ, INVALID_APK. */
DEXPP_C_API DexppApk *dexpp_apk_open(const char *path, DexppError **err);

/** Open an APK from its bytes in memory, for example bytes received over the
 *  network. The @p len bytes at @p bytes are copied, so the caller may free
 *  them as soon as this returns. They get the same checks as a file given to
 *  dexpp_apk_open(), and the APK handle then works the same.
 *
 *  @p label names the bytes at the start of error messages, where
 *  dexpp_apk_open() puts the path, and is also used by the errors of
 *  dexpp_apk_analysis(); NULL gives `"<apk>"`. @p bytes may be NULL when
 *  @p len is 0 (no bytes are not an APK, so this fails with INVALID_APK);
 *  NULL with a nonzero @p len also fails with INVALID_APK.
 *  Errors: INVALID_APK. */
DEXPP_C_API DexppApk *dexpp_apk_open_buffer(const uint8_t *bytes, size_t len, const char *label,
                                            DexppError **err);

/** Free an APK handle. Accepts NULL. */
DEXPP_C_API void dexpp_apk_free(DexppApk *apk);

/** The name of every archive entry. Free with dexpp_string_array_free(). */
DEXPP_C_API char **dexpp_apk_entries(const DexppApk *apk, size_t *count);

/** The uncompressed bytes of the entry @p name. Returns 1 and sets @p out
 *  (free with dexpp_buffer_free(); NULL for an empty entry) and @p length when
 *  the entry exists; returns 0 when it does not. */
DEXPP_C_API int dexpp_apk_read(const DexppApk *apk, const char *name, uint8_t **out,
                               size_t *length);

/** The manifest's package name, or NULL when there is no decodable
 *  manifest. */
DEXPP_C_API char *dexpp_apk_package(const DexppApk *apk);

/** Fill @p out with the decoded manifest. Returns 0 when the APK has no
 *  manifest or it cannot be decoded (then @p out is untouched). */
DEXPP_C_API int dexpp_apk_manifest(const DexppApk *apk, DexppManifest *out);

/** Free everything a DexppManifest owns. */
DEXPP_C_API void dexpp_manifest_clear(DexppManifest *manifest);

/** 1 when the APK has a decodable resources.arsc table. */
DEXPP_C_API int dexpp_apk_has_resources(const DexppApk *apk);

/** 1 when the APK has no resource table, or the table has no entries. */
DEXPP_C_API int dexpp_apk_resources_empty(const DexppApk *apk);

/** Fill @p out with the name of resource @p id (release with
 *  dexpp_resource_name_clear()). Returns 0 when there is no such id. */
DEXPP_C_API int dexpp_apk_resource_name(const DexppApk *apk, uint32_t id, DexppResourceName *out);

/** Free the strings of a DexppResourceName. */
DEXPP_C_API void dexpp_resource_name_clear(DexppResourceName *name);

/** The value of string resource @p id: returns 1 and fills @p out (release
 *  with dexpp_text_clear()); returns 0 when the id is missing or its value
 *  is not a string. A resource that refers to another resource is followed
 *  to the end. */
DEXPP_C_API int dexpp_apk_resolve_string(const DexppApk *apk, uint32_t id, DexppText *out);

/** Every value of resource @p id, one for each configuration the table gives
 *  it, in table order. Returns an array of @p count values (free with
 *  dexpp_resource_value_array_free()); NULL with @p count 0 when there is no
 *  resource table or no such id. A complex entry (a style, an array, a
 *  plural) has no single value and is left out. A value that refers to
 *  another resource is returned as it is. */
DEXPP_C_API DexppResourceValue *dexpp_apk_resource_values(const DexppApk *apk, uint32_t id,
                                                          size_t *count);

/** Free an array of DexppResourceValue and the strings in it. Accepts NULL. */
DEXPP_C_API void dexpp_resource_value_array_free(DexppResourceValue *values, size_t count);

/** The value of resource @p id as a boolean, read as Android reads it: a bool
 *  resource, any integer-typed value (nonzero is true), or a string ("1",
 *  "true" and "TRUE" are true, other text is false). Returns 1 and sets
 *  @p out to 1 or 0; returns 0 when there is no resource table, the id is
 *  missing, or its value is null or an empty string. A resource that refers
 *  to another resource is followed to the end, in the default
 *  configuration. */
DEXPP_C_API int dexpp_apk_resolve_bool(const DexppApk *apk, uint32_t id, int *out);

/** The value of resource @p id as an integer: an integer resource, or any
 *  other integer-typed value, read as its signed 32-bit data. Returns 1 and
 *  sets @p out; returns 0 when there is no resource table, the id is
 *  missing, or its value is of another type. A resource that refers to
 *  another resource is followed to the end. */
DEXPP_C_API int dexpp_apk_resolve_int(const DexppApk *apk, uint32_t id, int64_t *out);

/** The id of the resource with this type and entry name. Returns 1 and sets
 *  @p out, or 0 when there is none. */
DEXPP_C_API int dexpp_apk_resource_id(const DexppApk *apk, const char *type, const char *entry,
                                      uint32_t *out);

/** Fill @p out with the signature schemes and certificates (release with
 *  dexpp_signing_info_clear()). An unsigned APK gives all zeros. */
DEXPP_C_API void dexpp_apk_signing(const DexppApk *apk, DexppSigningInfo *out);

/** Free everything a DexppSigningInfo owns. */
DEXPP_C_API void dexpp_signing_info_clear(DexppSigningInfo *info);

/** A code-analysis context over every classes*.dex of the APK, in multidex
 *  order, loaded from the bytes the APK handle already holds. `threads` is as
 *  for dexpp_context_from_apk(): 0 lets the library choose, 1 keeps all work,
 *  the call graph included, on the calling thread.
 *
 *  The analysis is built on the first call and kept, one for each value of
 *  `threads`: a later call with the same value gives a new handle to the same
 *  analysis, so its call graph and other structures are built once. A call
 *  with another value builds a second analysis, which holds its own copy of
 *  the DEX files. So use one value in a program. Free the handle with
 *  dexpp_context_free(). Errors: INVALID_APK, INVALID_DEX. */
DEXPP_C_API DexppContext *dexpp_apk_analysis(const DexppApk *apk, uint32_t threads,
                                             DexppError **err);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* DEXPP_H */
