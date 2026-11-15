# C and Go

The C API is a C layer over the C++ library. It exists so that languages
that can call C, such as Go, can use dex++. The Go package is built on it.

Both cover the same ground as the [Python binding](quickstart.md): the whole
analysis API, the APK metadata, and the editable model a whole class at a
time. Only building or changing code inside a method needs [C++](cpp.md).

| Area | C | Go |
| ---- | - | -- |
| Load a DEX file, several DEX files, or an APK | `dexpp_context_from_dex`, `dexpp_context_from_dex_files`, `dexpp_context_from_apk` | `FromDEX`, `FromDEXFiles`, `FromAPK` |
| Classes, class references, methods, fields | `dexpp_class_*`, `dexpp_class_ref_*`, `dexpp_method_*`, `dexpp_field_*` | `Class`, `ClassRef`, `Method`, `Field` |
| Decoded bytecode | `dexpp_method_instructions`, `dexpp_opcode_name`, `dexpp_instruction_to_string` | `Method.Instructions`, `OpcodeName`, `ToString` |
| Control-flow graph, try blocks | `dexpp_method_cfg`, `dexpp_method_try_blocks` | `Method.Cfg`, `Method.TryBlocks` |
| Calls, field accesses, string loads, type uses, parameter registers | `dexpp_method_calls` and the functions after it | `Method.Calls` and the methods after it |
| Annotations, initial values of static fields | `dexpp_class_annotations`, `dexpp_field_initial_value`, ... | `Class.Annotations`, `Field.InitialValue`, ... |
| Call graph | `dexpp_call_graph_*` | `CallGraph` |
| Class hierarchy | `dexpp_class_hierarchy_*` | `ClassHierarchy` |
| Cross-references | `dexpp_xrefs_*` | `Xrefs` |
| APK: archive entries, manifest, resources, signing | `dexpp_apk_*` | `Apk`, `Manifest`, `ResourceTable`, `SigningInfo` |
| Editable model, a whole class at a time | `dexpp_model_*` | `Model` |
| Editing code inside a method | no | no |

Both are used from a checkout. There are no prebuilt binaries.

## Build the C library

```bash
cmake -S . -B build
cmake --build build --target dexpp_c -j
```

This gives two static libraries: `build/libdexpp_c.a` (the C layer) and
`build/libdexpp_core.a` (the C++ library). A C program links both, plus the
C++ runtime: `-lstdc++` on Linux, `-lc++` on macOS.

```bash
cc tool.c -Ibindings/c build/libdexpp_c.a build/libdexpp_core.a -lstdc++ -lm -o tool
```

With Visual Studio the libraries are `dexpp_c.lib` and `dexpp_core.lib`, in
the folder of the configuration you built (for example `build/Release`).

The headers are `bindings/c/dexpp.h` for analysis and
`bindings/c/dexpp_model.h` for the editable model. Their functions are listed
in the [C++ and C reference](reference/cpp.md).

### The shared library

A program that loads the C API at run time, such as the
[Java package](java.md) or another foreign function interface, needs a shared
library instead. It is not built by default:

```bash
cmake -S . -B build -DDEXPP_BUILD_C_SHARED=ON
cmake --build build --target dexpp_c_shared --config Release -j
```

This gives one file in `build`: `libdexpp_c_shared.so` on Linux,
`libdexpp_c_shared.dylib` on macOS, or `dexpp_c_shared.dll` on Windows. It
holds the C layer and the C++ library together. It exports the functions of
the two headers and nothing else, so it does not clash with other C++ code in
the same process.

## C

```c
#include <stdio.h>
#include "dexpp.h"

int main(void)
{
    DexppError *err = NULL;
    DexppContext *ctx = dexpp_context_from_apk("app.apk", 0, &err);
    if (ctx == NULL) {
        fprintf(stderr, "error %d: %s\n", (int)dexpp_error_code(err), dexpp_error_message(err));
        dexpp_error_free(err);
        return 1;
    }

    size_t count = 0;
    DexppClass **classes = dexpp_context_classes(ctx, &count);
    for (size_t i = 0; i < count; ++i) {
        char *name = dexpp_class_name(classes[i]);
        puts(name);
        dexpp_string_free(name);
    }
    dexpp_class_array_free(classes, count);
    dexpp_context_free(ctx);
    return 0;
}
```

The rules are the same for every function:

- **Handles are opaque pointers**: `DexppContext`, `DexppApk`, `DexppClass`,
  `DexppClassRef`, `DexppMethod`, `DexppField`, `DexppCallGraph`,
  `DexppClassHierarchy`, `DexppXrefs` and `DexppModelDex`. Free each one with
  its `_free` function.
- **Handles keep the analysis alive.** A class or method handle stays valid
  after the context handle it came from is freed.
- **Strings are copies.** Every string a function returns was allocated for
  you. Free it with `dexpp_string_free`. The string constants of the analysed
  program can contain NUL bytes, so they come back as a `DexppText`, which
  carries its length.
- **Arrays** come with a count. An empty array is `NULL` with a count of 0.
  Free an array with the `_array_free` function named for its type. It also
  frees what the elements own. To keep the handles of an array of handles,
  such as the classes from `dexpp_context_classes`, free the array alone with
  its `_array_free` function and a count of 0. `free()` works too when the
  library is linked statically, but not with a Windows DLL, whose C runtime
  can differ from the program's.
- **Structs filled through a pointer**, such as `DexppManifest` or
  `DexppSigningInfo`, own the memory their members point to. Release it with
  the matching `_clear` function, such as `dexpp_manifest_clear`.
- **Threads.** Reading from one analysis on several threads at once is safe.
  Do not free a handle while another thread still uses it.

### Errors in C

A function that can fail takes a `DexppError **err` as its last argument. On
failure it returns `NULL` (or 0) and sets `*err` to a new error object. On
success it sets `*err` to `NULL`, so `dexpp_error_free(err)` is always
allowed. Pass `NULL` for `err` when you do not need the details.

- `dexpp_error_code(err)` says what went wrong (see the table below).
- `dexpp_error_message(err)` describes it. The string belongs to the error.
- For `DEXPP_ERROR_INVALID_MODEL`, `dexpp_error_problem_count(err)` gives the
  number of broken rules, and `dexpp_error_problem_where` and
  `dexpp_error_problem_message` describe each one.
- `dexpp_error_free(err)` frees the error.

Each code matches an error of the other bindings:

| C code | Go sentinel | Python exception | Meaning |
| ------ | ----------- | ---------------- | ------- |
| `DEXPP_ERROR_FILE_NOT_FOUND` | `ErrFileNotFound` | `FileNotFoundError` | The path does not exist. |
| `DEXPP_ERROR_CANNOT_READ` | `ErrCannotRead` | `OSError` | The path exists but cannot be read, for example a directory. |
| `DEXPP_ERROR_CANNOT_WRITE` | `ErrCannotWrite` | `OSError` | The file cannot be created or written. |
| `DEXPP_ERROR_INVALID_APK` | `ErrInvalidAPK` | `InvalidApkError` | Not a ZIP archive, or an APK with no `classes.dex`. |
| `DEXPP_ERROR_INVALID_DEX` | `ErrInvalidDex` | `InvalidDexError` | A DEX file cannot be parsed. |
| `DEXPP_ERROR_UNSUPPORTED_DEX` | `ErrUnsupportedDex` | `UnsupportedDexError` | A DEX format the model does not read, such as version 41. |
| `DEXPP_ERROR_INVALID_MODEL` | `ErrInvalidModel` | `InvalidModelError` | A model breaks rules of the DEX format. |
| `DEXPP_ERROR_POOL_OVERFLOW` | `ErrPoolOverflow` | `PoolOverflowError` | A table of the written file would be too large. |
| `DEXPP_ERROR_VERSION_TOO_LOW` | `ErrVersionTooLow` | `VersionTooLowError` | A model uses a feature its DEX version does not have. |
| `DEXPP_ERROR_MODEL` | `ErrModel` | `ModelError` | A class to move or copy is missing, or already there. |
| `DEXPP_ERROR_INTERNAL` | `ErrInternal` | `MemoryError` or `RuntimeError` | An unexpected failure, such as running out of memory. |

## Go

The Go package is `github.com/burogurama/dexpp/bindings/go/dexpp`. It works on
Linux and macOS. It does not support Windows: cgo there needs a MinGW
toolchain, while the library is built with MSVC.

It links the two static libraries from the `build/` folder of the checkout,
so:

1. build the C library first, as above;
2. keep the build folder named `build`;
3. use the package from the checkout. `go get` alone does not work, because
   the libraries must be built first.

```bash
cd bindings/go
go test ./...
go run ./example ../../tests/data/sample.apk
```

The dexpp libraries are linked statically, so a Go program built with the
package needs no dexpp library at runtime. It uses the system's C++ runtime:
libstdc++ on Linux, libc++ on macOS.

The examples below are the package's testable examples, from
`bindings/go/dexpp/example_test.go`. `go test` runs each one and checks what
it prints. It runs them in `bindings/go/dexpp`, so they open the test files of
the repository by the relative path `../../../tests/data`.

### Classes and methods

```go
--8<-- "bindings/go/dexpp/example_test.go:load"
```

`FromAPK` loads every `classes*.dex` of an APK instead.
`dexpp.WithThreads(1)` keeps all its work on the calling thread:
`dexpp.FromAPK(path, dexpp.WithThreads(1))`.

### Bytecode and the control-flow graph

`Method.Instructions` returns the decoded bytecode. Each instruction is one of
24 struct types, one per family of opcodes, so a type switch tells them
apart:

```go
--8<-- "bindings/go/dexpp/example_test.go:instructions"
```

Pool operands, such as `method@3`, are raw indices into the tables of the DEX
file. The next section gives the names.

```go
--8<-- "bindings/go/dexpp/example_test.go:cfg"
```

### What a method refers to

`Calls`, `FieldAccesses`, `StringLoads` and `TypeUses` list what a method's
code refers to, with names instead of indices:

```go
--8<-- "bindings/go/dexpp/example_test.go:references"
```

Annotations are copied into Go values. An element value has one of the types
listed for `dexpp.AnnotationValue`: `nil`, `bool`, `int64`, `float64`,
`string`, a reference type such as `dexpp.TypeRef`, a slice, or a nested
`dexpp.Annotation`.

```go
--8<-- "bindings/go/dexpp/example_test.go:annotations"
```

### Call graph

Nodes and edges of the call graph are numbered. `CallNode` and `CallEdge` are
plain values that hold the numbers of their neighbours, and the methods of
`CallGraph` take and return numbers. `Nodes()` and `Edges()` copy the whole
table, so call them once; `Node(i)` and `Edge(i)` copy one entry.

```go
--8<-- "bindings/go/dexpp/example_test.go:callgraph"
```

`EdgesWhere(origin, kind)` filters the edges inside the library:
`g.EdgesWhere(dexpp.EdgeDeclared, dexpp.AnyInvokeKind)` gives one edge per
call site. See [Call graph](guide/call-graph.md) for what the edges and the
dispatch targets mean.

### Class hierarchy and cross-references

```go
--8<-- "bindings/go/dexpp/example_test.go:hierarchy"
```

### APK metadata

```go
--8<-- "bindings/go/dexpp/example_test.go:apk"
```

```go
--8<-- "bindings/go/dexpp/example_test.go:signing"
```

As in the other bindings, the signatures are only identified, not verified.

### Editing DEX files

`Model` is the editable model, a whole class at a time. It covers the same
operations as the Python `Model`: see [Editing DEX files](guide/editing-dex.md)
for what each one does.

```go
--8<-- "bindings/go/dexpp/example_test.go:model"
```

The second argument of `ReadModelFile` and the argument of `Write` say whether
to keep debug information: line numbers and local variable names.

### Errors in Go

A call that fails returns an `*dexpp.Error`. It has the `Code` of the C error,
its `Message`, and for an invalid model the `Problems`. Each code has a
sentinel value, so `errors.Is` tells the kinds apart, and
`errors.Is(err, fs.ErrNotExist)` is true for a missing file. Use `errors.As`
to read the code and the problems:

```go
--8<-- "bindings/go/dexpp/example_test.go:errors"
```

### Handles and memory in Go

- `Context`, `Apk`, `Class`, `ClassRef`, `Method`, `Field`, `CallGraph`,
  `ClassHierarchy`, `Xrefs` and `Model` are handles over C++ objects. The
  garbage collector frees them, and `Close()` frees one at once. Calling
  `Close()` twice, or on a nil handle, does nothing.
- Calling any other method on a closed or nil handle panics with a message
  that names the type, such as `dexpp: *Class used after Close`.
- Every handle keeps the analysis alive, so a `Class` still works after its
  `Context` is closed.
- Everything else (strings, slices, structs, the bytes from `Model.Write`) is
  copied into Go memory and needs no cleanup.
- A value that may be absent is never an empty string. A method that returns
  a handle returns `nil` when there is none, such as `Class.Superclass()`. A
  method that returns a plain value also returns a `bool`, such as
  `Class.SourceFile()`. A struct field that may be absent is a pointer, such
  as `Manifest.VersionName`.
- Reading from one analysis in several goroutines at once is safe. Do not
  call `Close()` on a handle while another goroutine still uses it.
