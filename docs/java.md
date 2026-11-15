# Java

The Java package calls the [C API](c-go.md) of dex++ through the Foreign
Function & Memory API of Java (the FFM API, part of Java since version 22). So
it needs Java 22 or newer, and no JNI code.

It covers the same ground as the Python and Go bindings: the whole analysis
API, the APK metadata, and the editable model a whole class at a time. Only
building or changing code inside a method needs [C++](cpp.md).

This page is the reference for building and using it: setup, errors and
memory. The [quick start](quickstart.md) and the guides show every area with
Java examples, next to Python and Go. The table below gives the names to look
for.

| Area | Java |
| ---- | ---- |
| Load a DEX file, several DEX files, or an APK | `AnalysisContext.fromDex`, `fromDexFiles`, `fromApk` |
| Classes, class references, methods, fields | `DexClass`, `ClassRef`, `DexMethod`, `DexField` |
| Decoded bytecode | `DexMethod.instructions`, `Dexpp.opcodeName`, `Dexpp.toString` |
| Control-flow graph, try blocks | `DexMethod.cfg`, `DexMethod.tryBlocks` |
| Calls, field accesses, string loads, type uses, parameter registers | `DexMethod.calls` and the methods after it |
| Annotations, initial values of static fields | `DexClass.annotations`, `DexField.initialValue`, ... |
| Call graph | `CallGraph` |
| Class hierarchy | `ClassHierarchy` |
| Cross-references | `Xrefs` |
| APK: archive entries, manifest, resources, signing | `Apk`, `Manifest`, `ResourceTable`, `SigningInfo` |
| Editable model, a whole class at a time | `Model` |
| Editing code inside a method | no |

The package is `io.github.burogurama.dexpp`. It is used from a checkout:
there is no package on Maven Central yet, and no prebuilt library.

## Build

The Java package loads the C API as a shared library. Build it first, from
the root of the checkout:

```bash
cmake -S . -B build -DDEXPP_BUILD_C_SHARED=ON
cmake --build build --target dexpp_c_shared --config Release -j
```

This gives one file in the `build` folder:

| System | File |
| ------ | ---- |
| Linux | `libdexpp_c_shared.so` |
| macOS | `libdexpp_c_shared.dylib` |
| Windows | `dexpp_c_shared.dll` |

The file holds the whole library. It exports the functions of the C API and
nothing else. It uses the C++ runtime of the system: libstdc++ on Linux,
libc++ on macOS, and the Visual C++ runtime on Windows.

Then build and test the Java package with Maven and a JDK 22 or newer:

```bash
cd bindings/java
mvn verify
```

The tests load the library from `../../build`. To use another file or
folder, add `-Ddexpp.library.path=PATH`. `mvn verify` makes
`target/dexpp-{{ dexpp_version }}.jar`, and `mvn install` puts that jar
into your local Maven repository as `io.github.burogurama:dexpp:{{ dexpp_version }}`.

## Run a program

A program that uses the package needs two settings when it starts:

- **Where the library is.** The package looks in this order:
    1. the system property `dexpp.library.path`: the library file, or the
       folder that holds it;
    2. each folder of `java.library.path`;
    3. the search path of the system: `LD_LIBRARY_PATH` on Linux,
       `DYLD_LIBRARY_PATH` on macOS, `PATH` on Windows.
- **Permission to call native code.** Java prints a warning when a library
  calls native code without it. Pass `--enable-native-access=ALL-UNNAMED`
  when the jar is on the class path. On the module path, the module name is
  `io.github.burogurama.dexpp`.

For example, from the root of the checkout:

```bash
java --enable-native-access=ALL-UNNAMED -Ddexpp.library.path=build \
     -cp bindings/java/target/dexpp-{{ dexpp_version }}.jar:. MyTool
```

## Java examples

The [quick start](quickstart.md) and the guides show a Java version of every
example: choose the "Java" tab above any example.

- [APK metadata](guide/apk.md): the manifest, components, resources, signing
  and archive entries.
- [Classes, methods and bytecode](guide/code.md): loading, classes, methods,
  instructions, the control-flow graph, try blocks and annotations.
- [Call graph](guide/call-graph.md), and
  [Class hierarchy and cross-references](guide/hierarchy-xrefs.md).
- [Editing DEX files](guide/editing-dex.md): the editable model.

The examples are in
`bindings/java/src/test/java/io/github/burogurama/dexpp/examples/`, one class
per guide. `ExamplesTest` runs each one and compares what it prints with the
output the guides show, saved in `bindings/java/src/test/resources/examples/`.
The tests run in `bindings/java`, so the examples open the test files of the
repository by the relative path `../../tests/data`. Each example runs in a
method that declares `throws IOException`.

## Errors

A file that cannot be opened is reported with the file exceptions of the JDK,
which are `IOException`s. Every other error is a `DexppException`. It is
unchecked, and `code()` gives the error code of the C API. Its subclasses
match the exceptions of the Python binding:

| C code | Java exception | Meaning |
| ------ | -------------- | ------- |
| `DEXPP_ERROR_FILE_NOT_FOUND` | `java.nio.file.NoSuchFileException` | The path does not exist. |
| `DEXPP_ERROR_CANNOT_READ` | `java.nio.file.FileSystemException` | The path exists but cannot be read, for example a folder. |
| `DEXPP_ERROR_CANNOT_WRITE` | `java.nio.file.FileSystemException` | The file cannot be created or written. |
| `DEXPP_ERROR_INVALID_APK` | `InvalidApkException` | Not a ZIP archive, or an APK with no `classes.dex`. |
| `DEXPP_ERROR_INVALID_DEX` | `InvalidDexException` | A DEX file cannot be parsed. |
| `DEXPP_ERROR_UNSUPPORTED_DEX` | `UnsupportedDexException` | A DEX format the model does not read, such as version 41. |
| `DEXPP_ERROR_INVALID_MODEL` | `InvalidModelException` | A model breaks rules of the DEX format; `problems()` lists them. |
| `DEXPP_ERROR_POOL_OVERFLOW` | `PoolOverflowException` | A table of the written file would be too large. |
| `DEXPP_ERROR_VERSION_TOO_LOW` | `VersionTooLowException` | A model uses a feature its DEX version does not have. |
| `DEXPP_ERROR_MODEL` | `ModelException` | A class to move or copy is missing, or already there. |
| `DEXPP_ERROR_INTERNAL` | `DexppException` | An unexpected failure, such as running out of memory. |

`InvalidModelException`, `PoolOverflowException` and `VersionTooLowException`
are subclasses of `ModelException`, as in Python. The file exceptions name
the file: `getFile()` gives the path, and `getReason()` says what is wrong.

```java
--8<-- "bindings/java/src/test/java/io/github/burogurama/dexpp/examples/ErrorsExamples.java:load"
```

```text
--8<-- "bindings/java/src/test/resources/examples/errors.load.txt"
```

Catch `DexppException` when any error of the library means the same to you:

```java
--8<-- "bindings/java/src/test/java/io/github/burogurama/dexpp/examples/ErrorsExamples.java:base"
```

```text
--8<-- "bindings/java/src/test/resources/examples/errors.base.txt"
```

The [editing guide](guide/editing-dex.md#errors) shows the exceptions of the
model, with the problems of an invalid model.

## Handles and memory

- `AnalysisContext`, `Apk`, `DexClass`, `ClassRef`, `DexMethod`, `DexField`,
  `CallGraph`, `ClassHierarchy`, `Xrefs` and `Model` are handles over C++
  objects. They are `AutoCloseable`, so `try`-with-resources frees them at
  the end of the block. `close()` frees one at once, and a second call does
  nothing. A handle that is never closed is freed by a `Cleaner` after the
  garbage collector finds it unused.
- Calling any other method on a closed handle throws an
  `IllegalStateException` that names the type, such as
  `dexpp: DexClass used after close()`.
- Every handle keeps the analysis alive, so a `DexClass` still works after its
  `AnalysisContext` is closed.
- Everything else (strings, lists, records, the bytes from `Model.write`) is
  copied into Java memory, and the C memory is freed before the method
  returns. Lists are immutable.
- A value that may be absent is never `null` or an empty string. It is an
  `Optional`, `OptionalInt` or `OptionalLong`, such as
  `DexClass.superclass()`, `ClassHierarchy.superclassOf(...)` and
  `Manifest.versionName()`.
- Access flags are an `AccessFlags` record with one constant per flag:
  `method.accessFlags().has(AccessFlags.STATIC)`.
- Reading from one analysis on several threads at once is safe. Do not close
  a handle while another thread still uses it.
