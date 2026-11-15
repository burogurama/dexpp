# dexpp Go binding

Go bindings for the dexpp Android DEX/APK static-analysis library, through its
C API (`bindings/c`) and cgo.

The package reaches the same API as the Python binding: loading, classes,
methods and fields, decoded bytecode, control-flow graphs and try blocks,
annotations, the call graph, the class hierarchy, cross-references, the
APK's manifest, resources and signing, and the editable DEX model a whole
class at a time. Only building or changing code inside a method needs the
C++ API.

## Build

The binding links the dexpp static libraries, so build them first (needs a
C++23 compiler — gcc 13+ / clang 19+ — and CMake ≥ 3.18):

```bash
# from the repo root
cmake -S . -B build
cmake --build build --target dexpp_c -j
```

This produces `build/libdexpp_c.a` and `build/libdexpp_core.a`, which the
package's cgo `LDFLAGS` reference by relative path. So the build folder must
be named `build`, and the package is used from the checkout, not through
`go get`. Then, from `bindings/go`:

```bash
go test ./...                 # the binding tests and the testable examples
go run ./example app.apk      # demo: package, components, code summary
```

The dexpp libraries are linked statically into the resulting Go binary, so it
has no dexpp shared-library dependency at runtime. It uses the system's C++
runtime: libstdc++ on Linux (`-lstdc++`), libc++ on macOS (`-lc++`). The
package supports Linux and macOS; it does not support Windows (cgo there needs
a MinGW toolchain, while the library is built with MSVC). The DEFLATE
decoder ([libdeflate](https://github.com/ebiggers/libdeflate), MIT) is compiled
into `libdexpp_core.a`.

## Usage

```go
import "github.com/burogurama/dexpp/bindings/go/dexpp"

apk, err := dexpp.OpenAPK("app.apk")
if err != nil { /* ... */ }
defer apk.Close()

if m, ok := apk.Manifest(); ok {
    fmt.Println(m.Package)
    for _, c := range m.Components {
        fmt.Println(c.Kind, c.Name, "reachable:", c.IsReachable)
    }
}

ctx, err := apk.Analysis()        // or dexpp.FromAPK / dexpp.FromDEX
if err != nil { /* ... */ }
defer ctx.Close()
for _, cls := range ctx.Classes() {
    for _, m := range cls.Methods() {
        for _, call := range m.Calls() {
            fmt.Println(cls.Name(), m.Name(), "calls", call.Target)
        }
    }
}

g, err := ctx.CallGraph()
if err != nil { /* ... */ }
loadURL := dexpp.MethodID{
    ClassDescriptor: "Landroid/webkit/WebView;", Name: "loadUrl", Proto: "(Ljava/lang/String;)V",
}
if n, ok := g.Find(loadURL); ok {
    for _, caller := range g.Callers(n.Index) {
        c, _ := g.Node(caller)
        fmt.Println("called by", c.ID)
    }
}
```

`example_test.go` holds a testable example for each area (loading,
bytecode, control-flow graph, references, annotations, call graph, hierarchy
and cross-references, APK metadata, signing, the model, errors). `go test`
runs them and checks their output, and the documentation site shows them on
its "C and Go" page.

| Area | Go API |
| ---- | ------ |
| Loading | `FromDEX`, `FromDEXFiles`, `FromAPK` (with `WithThreads`), `Context` |
| Code | `Class`, `ClassRef`, `Method`, `Field`, `AccessFlags`, `Annotation` |
| Bytecode | `Method.Instructions` (an `Instruction` per opcode family), `OpcodeName`, `ToString`, `Method.Cfg`, `Method.TryBlocks` |
| References | `Method.Calls`, `FieldAccesses`, `StringLoads`, `TypeUses`, `ParameterRegisters` |
| Whole program | `CallGraph`, `ClassHierarchy`, `Xrefs` |
| APK | `OpenAPK`, `Apk`, `Manifest`, `Component`, `ResourceTable`, `SigningInfo` |
| Editing | `Model`: `ReadModel`, `ReadModelFile`, `NewModel`, `CopyClass`, `MoveClass`, `RemoveClass`, `RenameTypes`, `Validate`, `PoolStats`, `Write` |

## Errors

A failed call returns an `*dexpp.Error` with a `Code`, a `Message`, and, for
a model that breaks rules of the DEX format, the list of `Problems`. Each code
has a sentinel value for `errors.Is`:

```go
_, err := dexpp.FromDEX("classes.dex")
switch {
case errors.Is(err, fs.ErrNotExist):     // also dexpp.ErrFileNotFound
case errors.Is(err, dexpp.ErrInvalidDex):
}
```

## Memory

`Context`, `Apk`, `Class`, `ClassRef`, `Method`, `Field`, `CallGraph`,
`ClassHierarchy`, `Xrefs` and `Model` are handles over C++ objects. Each is
freed by a finalizer on garbage collection; call `Close()` to release it
eagerly. Calling a method on a closed or nil handle panics with a clear
message instead of passing NULL to C. Handles keep the analysis alive on their
own, so a `Class` or `Method` stays valid after the `Context` it came from is
closed (matching the C++ handle model). Everything else — strings, slices,
structs, the DEX bytes from `Model.Write` — is copied into Go and needs no
cleanup.

A value that may be absent is never an empty string: a handle comes back as
`nil`, a plain value with a second `bool` result, and a struct field as a
pointer.
