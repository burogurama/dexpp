# C++

The C++ library is the core of dex++. The Python, C, Go and Java bindings are
built on it, and it can do more than they can: it can build and edit code
instruction by instruction.

This page shows how to add the library to a project and how its API is
organized. Every class and function is listed in the
[C++ reference](reference/cpp.md).

## Requirements

- A C++23 compiler:
    - Linux: GCC 13 or newer, or Clang 19 or newer. Older Clang versions
      cannot compile `<expected>` from libstdc++.
    - macOS: Apple Clang from Xcode 16 or newer, building for macOS 13.3 or
      newer (`std::format` needs it).
    - Windows: MSVC from Visual Studio 2022 or newer.
- CMake 3.18 or newer.

CI builds and tests the library on Linux x86_64 (GCC and Clang), macOS
(Apple Silicon and Intel) and Windows x64 (MSVC).

## Add it to a CMake project

There are no install rules yet, so the library is built as part of your
project. With `FetchContent`:

```cmake
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    dexpp
    GIT_REPOSITORY https://github.com/burogurama/dexpp.git
    GIT_TAG        v{{ dexpp_version }}
)
# Only the library: no unit tests and no C shim.
set(DEXPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DEXPP_BUILD_C OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(dexpp)

add_executable(my_tool main.cpp)
target_link_libraries(my_tool PRIVATE dexpp_core)
```

With a checkout inside your source tree, `add_subdirectory(dexpp)` does the
same.

`dexpp_core` is a static library. It contains its only dependency, the
libdeflate decompressor, so nothing else needs to be linked.

Your own code must be compiled as C++23, because the public headers use
`std::expected` and `std::span`.

## One header

```cpp
#include "dex.hpp"
```

`dex.hpp` includes the whole public API. `DEXPP_VERSION` is the library
version as a string.

| Namespace | What it holds | Entry point |
| --------- | ------------- | ----------- |
| `dex` | Code analysis | `dex::AnalysisContext` |
| `dex::apk` | The package: archive, manifest, resources, signing | `dex::apk::Apk` |
| `dex::model` | The editable DEX model | `dex::model::read`, `dex::model::write` |

## Open an APK

```cpp
--8<-- "cpp/analysis.cpp:open"
```

Functions that can fail return `std::expected<T, dex::Error>`. The library
does not throw for bad input.

`dex::Error` is the same type in every part of the library: the analysis and
APK loaders, the binary XML and resource table parsers, and the model. So one
function can open an APK, read a DEX file into a model and write it, and
return a single `std::expected`. The error has three fields:

- `code`, a `dex::ErrorCode`. It says what went wrong (see the table below).
- `message`, which says the same in words, with the path or the archive entry
  when there is one.
- `problems`, the list of broken rules when `code` is `InvalidModel`. It is
  empty for every other code.

| `dex::ErrorCode` | When |
| ---------------- | ---- |
| `FileNotFound` | The path does not exist. |
| `CannotRead` | The path exists but cannot be read, for example a directory. |
| `CannotWrite` | The file cannot be created or written. |
| `InvalidApk` | Not a ZIP archive, a damaged entry, or an APK with no `classes.dex`. |
| `InvalidDex` | A DEX file that cannot be parsed. |
| `UnsupportedDex` | A DEX file the model does not read, such as a DEX 041 container. |
| `InvalidModel` | The model breaks rules of the format; `problems` lists them. |
| `PoolOverflow` | Part of the file to write would be bigger than the format allows. |
| `VersionTooLow` | The model uses a feature its DEX version does not have. |
| `InvalidXml` | `dex::axml::parse` got something that is not binary XML. |
| `InvalidResources` | `dex::apk::ResourceTable::parse` got something that is not a resource table. |

The names match the Python exceptions (`InvalidDex` is
`dexpp.InvalidDexError`) and the C error codes (`DEXPP_ERROR_INVALID_DEX`).
A lookup that finds nothing is not an error: it returns `std::nullopt`, a null
pointer or an empty list.

The examples write every type in full, so you can see what each call returns.
In your own code `auto` works as well.

## Open an APK from memory

When the APK's bytes are already in memory, `dex::apk::Apk::from_buffer` opens
them without a file:

```cpp
--8<-- "cpp/analysis.cpp:memory"
```

- There are two overloads. One takes a `std::vector<uint8_t>` by value: pass
  it with `std::move` and the `Apk` takes its memory, so nothing is copied. The
  other takes a `std::span<const uint8_t>` and copies the bytes, so the caller
  may free them as soon as it returns.
- The bytes get the same checks as a file given to `open`. Bytes that are not a
  ZIP archive give an error with code `InvalidApk`.
- The second argument, the label, starts the error messages where `open` puts
  the path. It is `"<apk>"` when left out.

`Apk::analysis` takes the same `dex::ApkLoadOptions` as
`AnalysisContext::from_apk`. The result is kept for each options value: a
second call with equal options returns the same context, so its call graph and
other structures are built once. A call with other options builds a second
context, which holds its own copy of the DEX files. So use one setting in a
program.

## Walk the code

```cpp
--8<-- "cpp/analysis.cpp:code"
```

`Method::disassemble()` gives the method's code as text, one
`dex::DisassemblyLine` (an offset and a text) per instruction, with every
reference named: `invoke-virtual {v1, v0},
Ljava/io/PrintStream;->println(Ljava/lang/String;)V`. The
[code guide](guide/code.md#the-code-as-text) describes the format.

`Method::instructions()` gives the decoded instructions themselves.
`dex::Instruction` is a `std::variant` over 28 instruction structs. Use
`std::visit` or `std::get_if` to reach the fields of one kind, and
`dex::base_of` for the fields they all share: `offset`, `opcode` and `size`.
`dex::to_string` writes one instruction as text with raw pool indices
(`string@12`), since an instruction alone cannot name them.

## Ask the call graph

```cpp
--8<-- "cpp/analysis.cpp:graph"
```

The call graph, the class hierarchy and the cross-reference index are built on
first use and returned by reference. In C++ the graph works with node and edge
indices: `nodes()` and `edges()` are spans, and `callees`, `callers`,
`dispatch_targets` and `dispatch_sources` return indices into `nodes()`.

The ideas are the same as in the other languages. The guides explain them:
[code](guide/code.md), [call graph](guide/call-graph.md),
[hierarchy and cross-references](guide/hierarchy-xrefs.md).

## Lifetimes

- `Class`, `Method`, `Field` and `ClassRef` are small handles. Each keeps the
  analysis alive, so a handle stays valid after the `AnalysisContext` it came
  from is destroyed. Copying a handle is cheap.
- You get handles from the library, never by building them yourself: the
  constructors are private. So a handle always points at something; there is
  no null handle. A lookup that may find nothing returns `std::optional`:
  `AnalysisContext::find_class()` and `ClassRef::resolve()` give
  `std::nullopt` when no loaded DEX file defines the class.
- `std::string_view` and `std::span` results point into the analysis. They
  stay valid as long as the context, or any handle from it, is alive.
- The references returned by `call_graph()`, `class_hierarchy()`, `xrefs()` and
  `Method::cfg()` follow the same rule.
- One `AnalysisContext` may be read from several threads at the same time.

## Edit a DEX file

The model is plain data: a `dex::model::Dex` holds classes, a class holds
fields and methods, and a method holds its code as a list of items. There are
no indices into file tables and no byte offsets, so the structs can be changed
and moved freely.

```cpp
--8<-- "cpp/edit.cpp:read"
```

### Change code

```cpp
--8<-- "cpp/edit.cpp:edit"
```

Instructions in the model are *semantic*. There is one `Const` struct, not one
per encoding, and `write()` picks the shortest encoding that fits. Branches
name labels, and `write()` computes the offsets and grows a branch when its
target is too far.

### Check

```cpp
--8<-- "cpp/edit.cpp:validate"
```

`validate()` returns every rule of the format the model breaks. `write()` runs
the same checks and refuses an invalid model.

### Split and write

```cpp
--8<-- "cpp/edit.cpp:split"
```

The model's data types are in `model/types.hpp`, and its functions (`read`,
`write`, `validate`, `required_version`, `pool_stats`, `rename_types`) are in
`model/io.hpp`. [Editing DEX files](guide/editing-dex.md) explains the rules
that are the same in every language.

## Build and test the library itself

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure   # run from the repository root
```

With Visual Studio, which builds several configurations, name one:
`cmake --build build -j --config Debug` and `ctest --test-dir build -C Debug`.

The examples on this page are in `docs/examples/cpp/`. The test run builds and
runs them.
