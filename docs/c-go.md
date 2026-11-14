# C and Go

The C API is a small C layer over the C++ library. It exists so that languages
that can call C, such as Go, can use dex++. The Go package is built on it.

Both cover a **part** of the library:

| Area | C and Go |
| ---- | -------- |
| Load an APK or DEX file | yes |
| Manifest: package, components, exported, reachable | yes |
| Classes: list, find, superclass, interfaces | yes |
| Methods: name, descriptor, flags, number of calls | yes |
| The string table | yes |
| Editable model, a whole class at a time | yes |
| Bytecode, control-flow graphs | no |
| Call graph, class hierarchy, cross-references | no |
| Resources, signing | no |
| Editing code inside a method | no |

For the parts marked "no", use [Python](quickstart.md) or [C++](cpp.md).

Both are used from a checkout. There are no prebuilt binaries.

## Build the C library

```bash
cmake -S . -B build
cmake --build build --target dexpp_c -j
```

This gives two static libraries: `build/libdexpp_c.a` (the C layer) and
`build/libdexpp_core.a` (the C++ library). A C program links both, plus the
C++ runtime:

```bash
cc tool.c -Ibindings/c build/libdexpp_c.a build/libdexpp_core.a -lstdc++ -lm -o tool
```

The headers are `bindings/c/dexpp.h` for analysis and
`bindings/c/dexpp_model.h` for the editable model. Their functions are listed
in the [C++ and C reference](reference/cpp.md).

## C conventions

```c
#include <stdio.h>
#include "dexpp.h"

int main(void)
{
    char *err = NULL;
    DexppContext *ctx = dexpp_context_from_apk("app.apk", &err);
    if (ctx == NULL) {
        fprintf(stderr, "%s\n", err);
        dexpp_string_free(err);
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

- **Handles are opaque pointers**: `DexppContext`, `DexppClass`, `DexppMethod`,
  `DexppApk`, `DexppModelDex`. Free each one with its `_free` function.
- **Errors.** A function that can fail takes a `char **err`. On failure it
  returns `NULL` and sets `*err` to a message. Free the message with
  `dexpp_string_free`.
- **Strings are copies.** Every string a function returns was allocated for
  you. Free it with `dexpp_string_free`.
- **Arrays** are allocated too. Free an array with its matching
  `_array_free` function, which also frees the items in it.
- **Class and method handles keep the analysis alive.** They stay valid after
  the context handle is freed.

## Go

The Go package is `github.com/burogurama/dexpp/bindings/go/dexpp`. It links
the two static libraries from the `build/` folder of the checkout, so:

1. build the C library first, as above;
2. keep the build folder named `build`;
3. use the package from the checkout. `go get` alone does not work, because
   the libraries must be built first.

```bash
cd bindings/go
go test ./...
go run ./example ../../tests/data/sample.apk
```

The C++ runtime is linked statically. A Go program built with the package
needs only libc at runtime.

### Analysis

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

### Editing DEX files

`Model` is the editable model, a whole class at a time. It covers the same
operations as the Python `Model`: see [Editing DEX files](guide/editing-dex.md)
for what each one does.

```go
// Split a DEX file into one file per class.
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

The second argument of `ReadModelFile` and the argument of `Write` say whether
to keep debug information: line numbers and local variable names.

### Memory in Go

`Context`, `Apk`, `Class`, `Method` and `Model` wrap C++ objects. The garbage
collector frees them, and `Close()` frees one at once. Strings, component
lists and the bytes from `Model.Write` are copied into Go memory and need no
cleanup.
