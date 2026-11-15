# Java

The Java package calls the [C API](c-go.md) of dex++ through the Foreign
Function & Memory API of Java (the FFM API, part of Java since version 22). So
it needs Java 22 or newer, and no JNI code.

It covers the same ground as the Go package: the whole analysis API, the APK
metadata, and the editable model a whole class at a time. Only building or
changing code inside a method needs [C++](cpp.md).

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

The examples below come from the tests in
`bindings/java/src/test/java/io/github/burogurama/dexpp/ExamplesTest.java`.
The tests run each example and check what it prints, which is the output
shown after the code. They run in `bindings/java`, so they open the test files
of the repository by the relative path `../../tests/data`. Each example runs
in a method that declares `throws IOException`.

## Classes and methods

```java
--8<-- "bindings/java/src/test/java/io/github/burogurama/dexpp/ExamplesTest.java:load"
```

```text
--8<-- "bindings/java/src/test/resources/examples/load.txt"
```

`AnalysisContext.fromApk(path)` loads every `classes*.dex` of an APK instead.
`AnalysisContext.fromApk(path, 1)` keeps all its work on the calling thread.

## Bytecode and the control-flow graph

`DexMethod.instructions()` returns the decoded bytecode. `Instruction` is a
sealed interface. Each instruction is one of 24 records, one per family of
opcodes, so a pattern-matching `switch` tells them apart:

```java
--8<-- "bindings/java/src/test/java/io/github/burogurama/dexpp/ExamplesTest.java:instructions"
```

```text
--8<-- "bindings/java/src/test/resources/examples/instructions.txt"
```

Pool operands, such as `method@3`, are raw indices into the tables of the DEX
file. The next section gives the names.

```java
--8<-- "bindings/java/src/test/java/io/github/burogurama/dexpp/ExamplesTest.java:cfg"
```

```text
--8<-- "bindings/java/src/test/resources/examples/cfg.txt"
```

## What a method refers to

`calls()`, `fieldAccesses()`, `stringLoads()` and `typeUses()` list what a
method's code refers to, with names instead of indices:

```java
--8<-- "bindings/java/src/test/java/io/github/burogurama/dexpp/ExamplesTest.java:references"
```

```text
--8<-- "bindings/java/src/test/resources/examples/references.txt"
```

Annotations are copied into records. An element value is an
`AnnotationValue`: a sealed interface whose records are `NullValue`,
`BooleanValue`, `IntValue`, `FloatValue`, `StringValue`, `TypeValue`,
`EnumValue`, `FieldValue`, `MethodValue`, `MethodTypeValue`,
`MethodHandleValue`, `ArrayValue`, and a nested `Annotation`. A
`MethodHandleValue` has a `MethodHandleKind` and a `member`, which is a
`MethodId` or a `FieldId`: both implement the sealed interface `MemberId`.
Record patterns take a value apart:

```java
--8<-- "bindings/java/src/test/java/io/github/burogurama/dexpp/ExamplesTest.java:annotations"
```

```text
--8<-- "bindings/java/src/test/resources/examples/annotations.txt"
```

## Call graph

Nodes and edges of the call graph are numbered. `CallNode` and `CallEdge` are
records that hold the numbers of their neighbours, and the methods of
`CallGraph` take and return numbers. `nodes()` and `edges()` copy the whole
table, so call them once; `node(i)` and `edge(i)` copy one entry.

```java
--8<-- "bindings/java/src/test/java/io/github/burogurama/dexpp/ExamplesTest.java:callgraph"
```

```text
--8<-- "bindings/java/src/test/resources/examples/callgraph.txt"
```

`edgesWhere(origin, kind)` filters the edges inside the library. `null` for a
filter means any value, so `g.edgesWhere(EdgeOrigin.DECLARED)` gives one edge
per call site. See [Call graph](guide/call-graph.md) for what the edges and
the dispatch targets mean.

## Class hierarchy and cross-references

```java
--8<-- "bindings/java/src/test/java/io/github/burogurama/dexpp/ExamplesTest.java:hierarchy"
```

```text
--8<-- "bindings/java/src/test/resources/examples/hierarchy.txt"
```

## APK metadata

```java
--8<-- "bindings/java/src/test/java/io/github/burogurama/dexpp/ExamplesTest.java:apk"
```

```text
--8<-- "bindings/java/src/test/resources/examples/apk.txt"
```

```java
--8<-- "bindings/java/src/test/java/io/github/burogurama/dexpp/ExamplesTest.java:signing"
```

```text
--8<-- "bindings/java/src/test/resources/examples/signing.txt"
```

As in the other bindings, the signatures are only identified, not verified.

## Editing DEX files

`Model` is the editable model, a whole class at a time. It covers the same
operations as the Python `Model`: see [Editing DEX files](guide/editing-dex.md)
for what each one does.

```java
--8<-- "bindings/java/src/test/java/io/github/burogurama/dexpp/ExamplesTest.java:model"
```

```text
--8<-- "bindings/java/src/test/resources/examples/model.txt"
```

`Model.read`, `readFile`, `write` and `writeFile` keep debug information
(line numbers and local variable names) by default. Each one has a second
form with a `boolean debugInfo` argument; `false` leaves it out.

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
--8<-- "bindings/java/src/test/java/io/github/burogurama/dexpp/ExamplesTest.java:errors"
```

```text
--8<-- "bindings/java/src/test/resources/examples/errors.txt"
```

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
