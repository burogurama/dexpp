# Changelog

## Unreleased

### Platforms

- dex++ now builds and is tested on macOS (Apple Silicon and Intel, with Apple
  Clang) and on Windows x64 (with MSVC), next to Linux. CI runs the C++ tests
  and the Python tests on all three, and the Go tests on Linux and macOS.
- Releases now publish wheels for macOS 14 or newer (one for arm64, one for
  x86_64) and for Windows x64, next to the Linux ones.
- The Go package links libc++ on macOS. It does not support Windows.
- The Python module is now placed directly in the build folder with every
  CMake generator, so `PYTHONPATH=build` works with Visual Studio too.
- With MSVC, the library is compiled with Microsoft's
  `_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR`. Without it, code built with Visual
  Studio 2022 17.10 or newer crashes the first time it takes a lock when the
  process has loaded an older `msvcp140.dll`. A JVM loads the one in the
  JDK's `bin` folder, and JDK 22 ships an older one; a Python process can
  have loaded an older one too.

### Packaging

- dexpp works with Python 3.10 and newer again (0.2.0 needed 3.14). Releases
  publish wheels for CPython 3.10, 3.11, 3.12, 3.13 and 3.14 on every
  platform: Linux x86_64 (glibc and musl), macOS arm64 and x86_64, and Windows
  x64. CI runs the Python tests on 3.10 and 3.14, the oldest and the newest.
- The package metadata now lists the supported Python versions and macOS and
  Windows, next to Linux.

### Documentation

- dex++ now has a documentation site, <https://burogurama.github.io/dexpp/>:
  guides for APK metadata, code analysis, the call graph, cross-references and
  DEX editing, a Python reference, and a C++ and C reference. It is published
  from each release tag, so it describes the latest release.
- Every class, method and property in the Python type stubs now has a
  docstring, so editors show a description next to each completion.
- The examples in the guides are run by the tests, with the output the guides
  show.

### Python bindings

- The enums `InvokeKind`, `EdgeOrigin`, `EdgeKind`, `TypeRefKind`,
  `OperandWidth`, `AnnotationVisibility` and `ComponentKind` are now real
  Python enums, subclasses of `enum.Enum`, as the type stubs already said.
  `list(dexpp.InvokeKind)` lists the members, `isinstance(kind, enum.Enum)` is
  true, and the members can be pickled. The member names do not change, and
  `kind.name`, `kind.value`, `==` and hashing work as before. Breaking: a member
  is no longer a number, so `int(kind)` raises `TypeError`. Use `kind.value`,
  which gives the same number `int(kind)` gave. Passing anything other than a
  member of the right enum to `CallEdgeView.where` or `Manifest.components_of`
  raises `TypeError`.
- Every function and method now takes its arguments by keyword, under the
  names the type stubs give them. Before, 25 of them took positional arguments
  only, so a call such as `ctx.find_class(descriptor="LFoo;")` or
  `xrefs.string_refs(value="...")` passed the type checker and then failed with
  `TypeError`. The type stubs now also say that `Model.read` takes any
  bytes-like object (`bytes`, `bytearray`, `memoryview`), which it always
  accepted.
- `Class`, `Method`, `Field` and `ClassRef` objects now compare equal when they
  point at the same class, method or field of the same analysis, and they can
  be dictionary keys and set members. Before, `ctx.find_class("LA;") ==
  ctx.find_class("LA;")` was `False`, because each call makes a new Python
  object, and a `set` of methods did not remove duplicates. The same file
  loaded twice gives two analyses, and their objects are not equal.
- `TypeRef`, `EnumRef`, `FieldRef`, `MethodRef`, `MethodTypeRef`,
  `MethodHandleRef` and `ResourceName` could be compared but not hashed, so
  they could not be dictionary keys or set members. They now have a hash that
  matches their equality.
- Breaking: four properties that do a lot of work on every read are now
  methods, so they need `()`: `AnalysisContext.strings()` (copies every string
  of every DEX file), `Xrefs.referenced_strings()`, `Model.required_version()`
  (runs the whole validator) and `Model.pool_stats()` (walks the whole model).
  The rule, written in the Python reference, is that a property is cheap or
  cached and a method computes.
- `Method.instructions` is now a `dexpp.InstructionView`, a read-only
  sequence over the decoded bytecode, like `Cfg.blocks`. It copies nothing up
  front; before, every read of the property built a new list of every
  instruction. Indexing, slicing (which returns a `list`), `len()` and
  iteration work as before. Code that changed the list in place, or compared it
  with a list, must call `list(method.instructions)` first.
- `access_flags` (on `Class`, `Method`, `Field` and `ClassInfo`) is now a
  `dexpp.AccessFlags`, an `enum.IntFlag` with one member per DEX flag:
  `dexpp.AccessFlags.STATIC in method.access_flags`. It is still an `int`, so
  code that compared it with numbers keeps working.
- dexpp now raises its own exceptions, one per kind of error, where it raised
  a plain `ValueError` before:

  | Exception | Raised when |
  | --------- | ----------- |
  | `InvalidApkError` | a file is not a ZIP archive, or an APK has no `classes.dex` |
  | `InvalidDexError` | a DEX file cannot be parsed |
  | `UnsupportedDexError` | `Model.read` gets a format it does not read (DEX version 41) |
  | `InvalidModelError` | `Model.write` gets a model that breaks rules of the format; its `problems` attribute lists them |
  | `PoolOverflowError` | `Model.write` would need a table with more entries than the format allows |
  | `VersionTooLowError` | `Model.write` gets a model that needs a higher DEX version |
  | `ModelError` | `Model.copy_class` or `Model.move_class` cannot find the class, or the destination already has it; also the base class of the three above |

  All of them derive from `dexpp.Error`, and `dexpp.Error` derives from
  `ValueError`, so code that catches `ValueError` keeps working.
- `Model.read_file` and `Model.write_file` raised `FileNotFoundError` for every
  file problem. `read_file` now raises it only when the file does not exist,
  and `OSError` when the file exists but cannot be read. `write_file` raises
  `OSError` when the file cannot be created or written (breaking for code that
  caught `FileNotFoundError` around `write_file`).
- `AnalysisContext.from_dex`, `from_dex_files`, `from_apk` and `Apk.open` now
  raise `OSError` for a path that exists but cannot be read. A directory path
  made them raise `MemoryError` before (see "Fixes").
- `dexpp.Instruction` and `dexpp.AnnotationValue` now exist in the module. The
  type stubs already declared these two unions, but the module did not have
  them, so `isinstance(x, dexpp.Instruction)` raised `AttributeError`.

### C and Go bindings

- The C API (`bindings/c/dexpp.h`) now covers the whole analysis API, the same
  ground as the Python binding. Before, it had loading, classes, the names,
  descriptors and flags of methods, the string table, and the manifest's
  package and components. New: fields and class references, decoded
  instructions with `dexpp_opcode_name` and `dexpp_instruction_to_string`,
  control-flow graphs, try blocks, annotations with typed values and the
  initial values of static fields, a method's calls, field accesses, string
  loads, type uses and parameter registers, the call graph, the class
  hierarchy, the cross-reference index, and the whole manifest, resources,
  archive entries and signing information of an APK. The header's first
  comment gives the rules for memory, arrays and threads.
- **Breaking for C callers:** every function that can fail now takes a
  `DexppError **err` instead of a `char **err`. The error object gives a code
  (`dexpp_error_code`, one value per kind of failure, matching the Python
  exceptions), a message (`dexpp_error_message`) and, for a model that breaks
  rules of the DEX format, the list of problems
  (`dexpp_error_problem_count`, `dexpp_error_problem_where`,
  `dexpp_error_problem_message`). Free it with `dexpp_error_free`.
- Other changes that break C callers: `dexpp_context_from_apk` takes the
  number of threads; `dexpp_class_superclass` and `dexpp_class_interfaces`
  return `DexppClassRef` handles instead of strings; `dexpp_context_strings`
  returns `DexppText`, which keeps NUL bytes; `dexpp_method_call_count` is
  replaced by `dexpp_method_calls`; `dexpp_apk_components` is replaced by
  `dexpp_apk_manifest`; and `dexpp_class_info_free` is renamed
  `dexpp_class_info_clear`.
- The Go package now reaches the whole analysis API too: `Field`, `ClassRef`,
  `Method.Instructions` (an `Instruction` interface with one struct type per
  opcode family), `OpcodeName`, `ToString`, `Method.Cfg`, `Method.TryBlocks`,
  `Annotations` and `Field.InitialValue`, `Method.Calls`, `FieldAccesses`,
  `StringLoads`, `TypeUses` and `ParameterRegisters`, `CallGraph`,
  `ClassHierarchy`, `Xrefs`, `FromDEXFiles`, the `WithThreads` option of
  `FromAPK`, the whole `Manifest` with intent filters, `ResourceTable`,
  `Apk.Entries`, `Apk.Read`, `Apk.Signing` and `Version`.
- Go errors are now `*dexpp.Error` values with a `Code`, a `Message` and, for
  an invalid model, the `Problems`. Each code has a sentinel, such as
  `dexpp.ErrInvalidDex`, for `errors.Is`, and `errors.Is(err, fs.ErrNotExist)`
  is true for a missing file. Before, errors were plain strings.
- Changed Go signatures (breaking):
    - `Class.Superclass()` returns a `*ClassRef`, which is nil for
      `java.lang.Object`, instead of a string that was empty when there was
      no superclass. `Class.Interfaces()` returns `[]*ClassRef` instead of
      `[]string`.
    - The component kind constants are renamed: `Activity`, `Service`,
      `Receiver` and `Provider` are now `ComponentActivity`,
      `ComponentService`, `ComponentReceiver` and `ComponentProvider`.
    - `Apk.Components()` is replaced by `Apk.Manifest()`, which returns the
      whole manifest. In `Component`, `Exported` is now the attribute as the
      manifest writes it (a `*bool`, nil when missing), `IsExported` is the
      effective answer, and `Reachable` is renamed `IsReachable`.
    - `Apk.Package()` returns `(string, bool)`, false when the APK has no
      manifest, instead of an empty string.
    - `Class.AccessFlags()`, `Method.AccessFlags()` and
      `ClassInfo.AccessFlags` are a `dexpp.AccessFlags` instead of a `uint32`.
    - `Method.CallCount()` is removed. Its comment said it counted resolved
      calls, but it counted every invoke instruction. `len(m.Calls())` gives
      the same number.
- Go: calling a method on a closed or nil handle now panics with a message
  that names the handle type, instead of passing a null pointer to C.
  `Close()` on a nil handle does nothing.
- The C API can now be built as one shared library, `dexpp_c_shared`
  (`libdexpp_c_shared.so`, `libdexpp_c_shared.dylib` or
  `dexpp_c_shared.dll`), with the CMake option `-DDEXPP_BUILD_C_SHARED=ON`.
  It holds the C layer and the C++ library, and exports the `dexpp_`
  functions and nothing else. Every declaration in the C headers is marked
  with the new `DEXPP_C_API` macro for this; it is empty in every other
  build. The static `dexpp_c` that Go links does not change.
- The `_array_free` functions of handle arrays (classes, class references,
  methods, fields) are documented to free the array alone when the count is
  0, so a caller can keep the handles. Use this instead of `free()` with the
  shared library: on Windows, the DLL and the program can use different C
  runtimes.

### Java binding (new)

- dex++ has a Java package, `io.github.burogurama.dexpp`, in
  `bindings/java`, built with Maven. It needs Java 22 or newer: it calls the
  C API from the shared library through the Foreign Function & Memory API,
  with no JNI code. It reaches the same API as the Go package: loading DEX
  files and APKs, classes, methods and fields, decoded bytecode, control-flow
  graphs, try blocks, annotations, the call graph, the class hierarchy,
  cross-references, the manifest, resources and signing of an APK, and the
  editable model a whole class at a time. It works on Linux, macOS and
  Windows, from a checkout; it is not on Maven Central yet.
- Handles (`AnalysisContext`, `Apk`, `DexClass`, `ClassRef`, `DexMethod`,
  `DexField`, `CallGraph`, `ClassHierarchy`, `Xrefs`, `Model`) are
  `AutoCloseable`, and a `Cleaner` frees the ones that are never closed.
  Everything else is copied into Java records and immutable lists. A value
  that may be absent is an `Optional`, `OptionalInt` or `OptionalLong`.
- Instructions are a sealed interface with one record per kind, and
  annotation values are a sealed interface too, so a pattern-matching
  `switch` tells them apart. Access flags are an `AccessFlags` record with
  one constant per flag.
- A file that cannot be opened throws `NoSuchFileException` or
  `FileSystemException`. Every other error throws an unchecked
  `DexppException` that carries the C error code. Its subclasses match the
  Python exceptions: `InvalidApkException`, `InvalidDexException`,
  `UnsupportedDexException`, `ModelException`, and below it
  `InvalidModelException` (with the list of problems),
  `PoolOverflowException` and `VersionTooLowException`.
- The documentation site has a Java page, with examples that the tests run.
  CI builds the shared library and runs the Java tests on Linux, macOS and
  Windows, with Java 22 and Java 25.

### Fixes

- A damaged APK Signing Block could make `parse_signing_info` (and so
  `Apk::signing()` / `Apk.signing`) read outside the APK's memory, or loop
  forever. Two size checks were written as `a + b > c`, and a huge size taken
  from the file wrapped around and passed them. The fuzzer found it; the checks
  now compare by subtraction.
- The manifest now reads the `android:` attributes by resource id, as Android
  does, instead of by name. Before, an app could hide `android:exported` (and
  so change `is_exported` and `is_reachable`) by renaming the attribute's name
  string, or add an attribute that is only named `exported` and have dexpp
  read it although Android ignores it. The binary-XML decoder now gives each
  attribute its `resource_id` and each document `has_resource_map`, and
  `XmlNode::attribute_by_id()` finds an attribute by id. For a document
  without a resource map (not written by aapt or aapt2), the manifest falls
  back to the attribute name in the android namespace.
- Go: the short methods of `Class` and `Method` (`Name`, `Package`,
  `Superclass`, `AccessFlags` and others), and the early-return paths of
  `Classes`, `Strings`, `Interfaces`, `Methods` and `Components`, did not keep
  their handle alive during the C call. In code such as
  `ctx.FindClass(d).Name()`, the garbage collector could free the handle while
  C was still using it. Every method now keeps its handle alive until it
  returns.
- `Model.read` in Python read the wrong memory when it got a buffer whose bytes
  are not next to each other, such as a reversed or strided `memoryview`: it
  read forward from the buffer's start and could go past its end. It now reads
  the bytes in order, copying them first when the buffer is not contiguous.
- Loading a path that is a directory no longer fails with an out-of-memory
  error. `AnalysisContext::from_dex`, `from_dex_files`, `from_apk`,
  `apk::Apk::open` and `raw::parser::parse_file` read the size of the
  "file", got a meaningless value, and tried to allocate that much memory.
  They now return an error. The new error code `AnalysisError::Code::CannotRead`
  means "the path exists but cannot be read"; `FileNotFound` now means only
  that the path does not exist.
- `model::read_file` no longer lets a `std::ios_base::failure` exception
  escape for a path it can open but not read, such as a directory. It returns
  an `Io` error.
- Every loader now reads its file through one function,
  `raw::read_whole_file`, which does not trust a file size it did not read.
- `model::write_file` (and `Model.write_file` in Python,
  `dexpp_model_write_file` in C, `Model.WriteFile` in Go) could report success
  when the file was not written, for example on a full disk: the error showed
  up only when the file was closed, and that result was not checked. It is now
  reported as `CannotWrite` (an `OSError` in Python).
- C API: a function that returns an empty string no longer passes a null
  pointer to `memcpy`, which is undefined behaviour. `dexpp_class_package` did
  this for every class in the default package. The comment on
  `dexpp_class_package` in `dexpp.h` also said the package comes back as
  `pkg.sub`; it comes back as `pkg/sub`, and the comment now says so.

### C++ API

- `Class`, `Method`, `Field` and `ClassRef` now have `operator==` and a
  `std::hash` specialization, so they can be compared and used as keys of
  `std::unordered_map` and `std::unordered_set`. Two handles are equal when
  they come from the same analysis (one `AnalysisContext`, or copies of it) and
  point at the same class, method or field. Handles from two contexts that
  loaded the same file are not equal. A default-constructed `Class` equals only
  another default-constructed one.

### C++ API (breaking)

- `AnalysisError::Code` lost four values that the library never produced:
  `MalformedBytecode`, `UnresolvedReference`, `UnsupportedInstruction` and
  `InvalidDescriptor`. The remaining values are `FileNotFound`, `CannotRead`,
  `InvalidDexFile` and `InvalidApk`. Code that named one of the removed values
  no longer compiles; it could never have received one.
- `dex::build_cfg` is no longer public. It took a `raw::CodeItem`, a type of
  the internal parser layer, so it was the one public function that tied users
  to that layer. `Method::cfg()` is the way to get a control-flow graph. The
  public headers no longer include any `raw/` header.
- `model::Error::Kind::Io` is replaced by three kinds, so a caller can tell the
  cases apart without reading the message: `FileNotFound` (`read_file`: the
  path does not exist), `CannotRead` (`read_file`: the path exists but cannot
  be read, for example a directory) and `CannotWrite` (`write_file`: the file
  cannot be created or written). These match the analysis side's
  `AnalysisError::Code::FileNotFound` and `CannotRead`.
- In the parser layer, `raw::ParseError::Code::NullPointer`, which
  `raw::parser::parse_file` used for a file it could not open, is replaced by
  `FileNotFound` and `CannotRead`.

### Build

- dex++'s own sources are now compiled with `-Wall -Wextra`, and the warnings
  GCC 13 and Clang 19 reported are fixed. The new CMake option
  `DEXPP_WERROR=ON` turns a warning into an error. It is off by default, so a
  newer compiler that warns about something new does not break a build from
  source; CI builds with it on.
- The C API now has its own tests (`tests/c/`), which call every function of
  `dexpp.h` and `dexpp_model.h`. They are part of the unit tests, so the
  sanitizer run reports a leak or a double free in the C layer.

## 0.2.0

### Editable DEX model (new)

`dex::model` (include `model/model.hpp`, or `dex.hpp`) reads a DEX file into
plain C++ structs, lets you change them or build new ones from scratch, and
writes a valid DEX file again.

- The model holds no pool indices and no byte offsets. References are written
  out in full (type descriptors, prototypes, `MethodId`, `FieldId`) and
  branches point at labels. `write()` collects the references, sorts and
  numbers the pools, and picks each instruction's encoding (`Const` becomes
  `const/4`, `const/16`, `const/high16` or `const`; a branch grows to
  `goto/16`, `goto/32`, or a reversed `if` around a `goto/32` when it does not
  reach). So splitting a file, merging files, renaming classes and generating
  code are ordinary edits followed by `write()`.
- `read()` / `read_file()`, `write()` / `write_file()`, `validate()` (the
  rules of ART's DEX file verifier that apply, plus what the encoding needs),
  `required_version()`, `pool_stats()` and `rename_types()` (every type
  reference, including the class names inside generic signatures).
- Strings keep unpaired surrogates (as WTF-8), floats keep their exact bits,
  and values keep their exact type (a `char` stays a `char`), so a file
  survives a read and a write unchanged in meaning. Writing is deterministic,
  and writing a re-read output gives the same bytes again.
- Checked on 776 DEX files from 253 real apps: all but one round-trip (the
  exception is a corrupt file that ART also rejects), and ART's verifier
  (through the SDK's `dexdump`) accepts every written file.
- Speed and memory on the largest of them (42 MB, 6.9M instructions): read in
  0.6 s, write in 1.6 s; the model takes about 15 times the file size.

From Python and Go, the model works a whole class at a time. Building or
editing a class instruction by instruction stays in C++.

- Python: `dexpp.Model` reads a DEX file (`Model.read`, `Model.read_file`) or
  starts empty (`Model(version)`). It lists and inspects classes
  (`class_descriptors`, `class_info`, `class_interfaces`), copies, moves and
  removes whole classes between models (`copy_class`, `move_class`,
  `remove_class`), renames types (`rename_types`), and checks and writes the
  result (`validate`, `pool_stats`, `required_version`, `write`,
  `write_file`). Errors raise `FileNotFoundError` or `ValueError`.
- C and Go: `bindings/c/dexpp_model.h` gives the same operations on an opaque
  `DexppModelDex` handle, and the Go package wraps them as `Model`
  (`ReadModel`, `ReadModelFile`, `NewModel`, `CopyClass`, `MoveClass`,
  `RemoveClass`, `RenameTypes`, `Validate`, `PoolStats`, `Write`, ...).

### Packaging

- dexpp is now on PyPI: `pip install dexpp`. The wheels are also still
  attached to each GitHub release.
- dexpp now needs Python 3.14 or newer (breaking: 0.1.0 worked with 3.9 and
  newer). The wheels are built for CPython 3.14 only.

### Smaller changes

- `MethodId`, `FieldId` (and their hashes), `AccessFlags` and
  `AnnotationVisibility` moved to `src/common/`, so the analysis layer and
  the model share them. Their names and namespace are unchanged, and the old
  headers still work. `AccessFlags` gained `Bridge` and `Varargs`.
- `dex::raw` gained what writing needs: one opcode table for all 256 opcodes
  with an instruction decoder and encoder (`raw/opcodes.hpp`; the analysis
  decoder now uses it too), lossless MUTF-8 decoding and encoding
  (`raw/mutf8.hpp`), name and descriptor rules (`raw/descriptors.hpp`),
  SHA-1 / SHA-256 (`raw/digest.hpp`, moved from APK signing), a debug-info
  parser (`parse_debug_info`), and a byte writer (`raw/byte_writer.hpp`).
- `raw::parser::compute_signature` is now implemented. `verify_offsets`, which
  was declared but never implemented, is removed.
- `compute_checksum` is several times faster.
- The raw parser no longer negates `INT32_MIN` (undefined behavior) when a
  catch handler declares that size.

### Call graph: virtual dispatch is stored once per method (breaking)

A virtual or interface call can reach every override of the called method in
the app. These possible targets depend only on the called method, but the
call graph used to store them again at every call site, as one edge per site
and per target. On large apps that came to tens or hundreds of millions of
edges, and on the largest (for example the Facebook APK, 164K classes) the
call graph ran out of memory.

They are now stored once per called method, and the graph is built with
integer ids instead of strings. It holds the same information, in far less
time and memory (building the call graph after loading the APK; peak memory
includes the loaded APK):

| App | Stored before | Stored now | Build time before → now | Peak memory before → now |
| --- | --- | --- | --- | --- |
| Facebook | 181M edges | 3.7M edges + 206K dispatch entries | ran out of memory → 0.54 s | over 10.9 GB → 1.05 GB |
| GitHub | 89M edges | 0.76M edges + 74K dispatch entries | 4.4 s → 0.13 s | 5.8 GB → 0.21 GB |
| Instagram | 89M edges | 2.3M edges + 168K dispatch entries | 8.0 s → 0.36 s | 5.9 GB → 0.68 GB |
| YouTube | 31M edges | 1.4M edges + 105K dispatch entries | 3.9 s → 0.23 s | 2.8 GB → 0.46 GB |

Building the call graph also no longer decodes every instruction into the
context's instruction cache, and no longer builds the class hierarchy.

For an app split over several large DEX files, part of the build runs on one
thread per DEX file. `ApkLoadOptions::threads` (Python: `from_apk(threads=...)`)
now also sets these threads: `1` keeps the whole build on the calling thread.
The result never depends on the number of threads.

API changes, in C++ (`dex::CallGraph`) and Python (`dexpp.CallGraph`):

- `edges()` holds one `Declared` edge per call site, plus the
  `InheritedResolution` edge when there is one. `EdgeOrigin::ChaOverride`
  (`EdgeOrigin.CHA_OVERRIDE`) is removed.
- `dispatch_targets(node)` lists the overrides a virtual or interface call to
  that method may reach, and `dispatch_sources(node)` is the reverse.
  `dispatch_targets(edge)` gives the targets that apply at one call site (empty
  for super, direct and static calls). Python: `CallNode.dispatch_targets`,
  `CallNode.dispatch_sources`, `CallEdge.dispatch_targets`.
- `callees(node)` and `callers(node)` list every method a method may call, or
  be called by, with virtual dispatch included. Python: `CallNode.callees`,
  `CallNode.callers`.
- `CallNode.outgoing` / `incoming` (and `out_edges` / `in_edges` in Python) no
  longer include dispatch targets. Use `callees` / `callers` for those.
- The deprecated Python property `CallEdge.via_override` is removed.

## 0.1.0 — first release

The first public release. The API is still alpha: it may change in later
0.x releases.

### APK metadata (`dex::apk` / `dexpp.Apk`)

- Archive listing and entry extraction (stored and deflate entries; no ZIP64).
- Binary `AndroidManifest.xml` decoding: package, version, SDK levels,
  permissions, declared permissions and their protection levels, components
  with their exported / intent-filter / permission attributes, launcher
  activity, `debuggable`, `allowBackup`.
- `Component.is_reachable`: exported and not gated behind an app-declared
  `signature` permission.
- `resources.arsc` decoding: string resolution, resource names, reverse
  lookup.
- Signing scheme detection (v1, v2, v3/v3.1) and X.509 certificate details
  with SHA-1 / SHA-256 fingerprints. Signatures are identified, not verified.

### Code analysis (`dex` / `dexpp.AnalysisContext`)

- Load a single DEX, several DEX files, or every `classes*.dex` of an APK.
  Loading is lazy: the file is memory-mapped and only what a query touches is
  decoded. Multidex APKs are decompressed in parallel.
- Classes, methods, fields, access flags, annotations, and static field
  initial values.
- Decoded bytecode with mnemonics, try/catch blocks, register frames, and a
  control-flow graph per method.
- Per-method forward references: calls, field accesses, string loads, type
  uses.
- Whole-program call graph with class-hierarchy-aware virtual dispatch, a
  class hierarchy, and a cross-reference index (who calls / reads / writes /
  loads what).

### Python bindings

- One extension module with type stubs (`mypy` and `pyright`).
- The call graph, xrefs, and CFG tables are zero-copy views; call-graph nodes
  and edges can be navigated directly.
- `dexpp.__version__`.

### Packaging

- Prebuilt wheels for Linux x86_64 (glibc and musl), attached to the GitHub
  release. The C++ library and the C / Go bindings are source-only
  for now.
- No system dependencies: DEFLATE decoding uses libdeflate, compiled in.

### Robustness

- The parsers are hardened against malformed and hostile input (bounds
  checks, allocation limits against oversized counts and zip bombs).
