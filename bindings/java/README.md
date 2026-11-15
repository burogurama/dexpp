# dexpp Java binding

The Java package of dex++, `io.github.burogurama.dexpp`. It calls the C API
of dex++ (`bindings/c`) through the Foreign Function & Memory API of Java, so
it needs **Java 22 or newer**, and no JNI code.

It reaches the same API as the Go binding: loading, classes, methods and
fields, decoded bytecode, control-flow graphs and try blocks, annotations,
the call graph, the class hierarchy, cross-references, the APK's manifest,
resources and signing, and the editable DEX model a whole class at a time.
Only building or changing code inside a method needs the C++ API.

It is used from a checkout. It is not on Maven Central yet.

## Build

The package loads the C API as a shared library, so build that first, from
the root of the repository (needs a C++23 compiler and CMake 3.18 or newer):

```bash
cmake -S . -B build -DDEXPP_BUILD_C_SHARED=ON
cmake --build build --target dexpp_c_shared --config Release -j
```

This makes `build/libdexpp_c_shared.so` (Linux),
`build/libdexpp_c_shared.dylib` (macOS) or `build/dexpp_c_shared.dll`
(Windows). Then, from `bindings/java`, with Maven and a JDK 22 or newer:

```bash
mvn verify      # builds target/dexpp-<version>.jar and runs the tests
mvn install     # also puts the jar in the local Maven repository
```

The tests load the library from `../../build`. Add
`-Ddexpp.library.path=PATH` to use another file or folder.

## Run

A program that uses the package needs:

- the jar on its class path (or module path; the module name is
  `io.github.burogurama.dexpp`);
- the library: the system property `dexpp.library.path` names the file or
  its folder. Without it, the package looks in `java.library.path`, then in
  the system's search path (`LD_LIBRARY_PATH`, `DYLD_LIBRARY_PATH` or
  `PATH`);
- `--enable-native-access=ALL-UNNAMED` (or the module name), or Java prints
  a warning when the package loads the library.

```bash
java --enable-native-access=ALL-UNNAMED -Ddexpp.library.path=build \
     -cp bindings/java/target/dexpp-<version>.jar:. MyTool
```

## Usage

The guides of the documentation site show every example in Java, next to
Python and Go: <https://burogurama.github.io/dexpp/>. The examples are in
`src/test/java/io/github/burogurama/dexpp/examples/`, and `ExamplesTest`
runs them and checks what they print. `docs/java.md`
(<https://burogurama.github.io/dexpp/java/>) covers setup, errors and memory.

- `AnalysisContext.fromDex`, `fromDexFiles` and `fromApk` load code;
  `Apk.open` opens an APK for its manifest, resources and signatures;
  `Model.readFile` reads a DEX file into the editable model.
- Handles (`AnalysisContext`, `DexClass`, `DexMethod`, `Model`, ...) are
  `AutoCloseable`; a `Cleaner` frees the ones that are never closed.
- Values that may be absent are `Optional`s. Instructions are a sealed
  interface with one record per kind, for pattern matching.
- A file that cannot be opened throws a `NoSuchFileException` or another
  `IOException`. Everything else throws an unchecked `DexppException`, with
  subclasses such as `InvalidDexException` and `InvalidModelException`.

## Layout

- `src/main/java/.../Native.java`: one Java method per C function, written
  by hand with `Linker.downcallHandle`. `Layouts.java` describes the C
  structs. `HeaderTest` reads the two C headers and checks that both match
  them.
- The other classes are the public API. They copy every C result into Java
  objects and free the C memory at once.
