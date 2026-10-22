# dexpp Go binding

Go bindings for the dexpp Android DEX/APK static-analysis library, via its C ABI
(`bindings/c`) and cgo.

## Build

The binding links the dexpp static libraries, so build them first (needs a
C++23 compiler — gcc 13+ / clang 19+ — and CMake ≥ 3.18):

```bash
# from the repo root
cmake -S . -B build
cmake --build build --target dexpp_c -j
```

This produces `build/libdexpp_c.a` and `build/libdexpp_core.a`, which the
package's cgo `LDFLAGS` reference by relative path. Then, from `bindings/go`:

```bash
go test ./...                 # run the binding tests
go run ./example app.apk      # demo: package, components, class/method summary
```

The C++ runtime is linked statically into the resulting Go binary (`-lstdc++`),
so it has no dexpp shared-library dependency at runtime — only libc. The DEFLATE
decoder ([libdeflate](https://github.com/ebiggers/libdeflate), MIT) is compiled
into `libdexpp_core.a`.

## Usage

```go
import "github.com/burogurama/dexpp/bindings/go/dexpp"

apk, err := dexpp.OpenAPK("app.apk")
if err != nil { /* ... */ }
defer apk.Close()

fmt.Println(apk.Package())
for _, c := range apk.Components() {
    fmt.Println(c.Kind, c.Name, "reachable:", c.Reachable)
}

ctx, _ := apk.Analysis()          // or dexpp.FromAPK / dexpp.FromDEX
defer ctx.Close()
for _, cls := range ctx.Classes() {
    for _, m := range cls.Methods() {
        fmt.Println(cls.Name(), m.Name()+m.Descriptor())
    }
}
if c := ctx.FindClass("Lcom/example/Foo;"); c != nil { /* ... */ }
```

## Editing DEX files (the model)

`Model` wraps dexpp's editable DEX model (`dex::model`): read a DEX file into
memory, change it at the granularity of whole classes, and write a DEX file
again. Editing inside a method is not exposed — a class crosses the boundary
whole — which covers splitting, merging and renaming.

```go
// Split a DEX into one file per class.
m, err := dexpp.ReadModelFile("classes.dex", true)
if err != nil { /* ... */ }
defer m.Close()

for i, desc := range m.ClassDescriptors() {
    one := dexpp.NewModel(m.Version())
    if err := one.CopyClass(m, desc); err != nil { /* ... */ }
    bytes, err := one.Write(true)
    if err != nil { /* ... */ }
    _ = os.WriteFile(fmt.Sprintf("class%d.dex", i), bytes, 0o644)
    one.Close()
}

// Rename a class everywhere it is referenced, then write.
m.RenameTypes(map[string]string{"Lcom/old/Name;": "Lcom/new/Name;"})
if problems := m.Validate(); len(problems) == 0 {
    out, _ := m.Write(true)
    _ = os.WriteFile("renamed.dex", out, 0o644)
}
```

`PoolStats()` reports how full each DEX pool would be (the format indexes types,
protos, fields and methods with 16 bits), so a splitter can keep each file under
the limit. `RequiredVersion()` gives the lowest DEX version the model needs.

## Memory

`Context`, `Apk`, `Class`, `Method`, and `Model` are handles over C++ objects.
Each is freed by a finalizer on garbage collection; call `Close()` to release
eagerly. Returned strings, `[]Component`, and the DEX bytes from `Model.Write`
are copied into Go and need no cleanup. `Class`/`Method` handles keep the
analysis alive on their own, so they stay valid after the owning `Context` is
closed (matching the C++ handle model). `Model` owns its data outright; a
`Class`/`Method` from the analysis API is unrelated to it.

## Surface

Two C headers back the binding:

- `bindings/c/dexpp.h` — the analysis API: loading (APK / DEX), the manifest
  (package, components, exported/reachable), class enumeration and lookup, the
  string pool, and per-class methods with descriptors and access flags.
- `bindings/c/dexpp_model.h` — the editable model (above): read, inspect
  classes, move/copy/remove whole classes, rename types, validate, pool stats,
  and write.

Instruction-level disassembly, the call graph, the xref index (analysis side),
and building classes instruction by instruction (model side) are in the
C++/Python APIs and can be added to the C shim as needed.
