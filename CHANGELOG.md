# Changelog

## Unreleased

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

### Fixes

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
- C API: a function that returns an empty string no longer passes a null
  pointer to `memcpy`, which is undefined behaviour. `dexpp_class_package` did
  this for every class in the default package. The comment on
  `dexpp_class_package` in `dexpp.h` also said the package comes back as
  `pkg.sub`; it comes back as `pkg/sub`, and the comment now says so.

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
