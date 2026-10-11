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

## Memory

`Context`, `Apk`, `Class`, and `Method` are handles over C++ objects. Each is
freed by a finalizer on garbage collection; call `Close()` to release eagerly.
Returned strings and `[]Component` are copied into Go and need no cleanup.
`Class`/`Method` handles keep the analysis alive on their own, so they stay
valid after the owning `Context` is closed (matching the C++ handle model).

## Surface

The C ABI (`bindings/c/dexpp.h`) currently covers loading (APK / DEX), the
manifest (package, components, exported/reachable), class enumeration and
lookup, the string pool, and per-class methods with descriptors and access
flags. Instruction-level disassembly, the call graph, and the xref index are
exposed in the C++/Python APIs and can be added to the C shim as needed.
