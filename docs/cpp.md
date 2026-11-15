# C++

The C++ library is the core of dex++. The Python, C and Go bindings are built
on it, and it can do more than they can: it can build and edit code
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

Functions that can fail return `std::expected`. The error has a `code` and a
`message`. The library does not throw for bad input.

The examples write every type in full, so you can see what each call returns.
In your own code `auto` works as well.

## Walk the code

```cpp
--8<-- "cpp/analysis.cpp:code"
```

`dex::Instruction` is a `std::variant` over 24 instruction structs. Use
`std::visit` or `std::get_if` to reach the fields of one kind, and
`dex::base_of` for the fields they all share: `offset`, `opcode` and `size`.

## Ask the call graph

```cpp
--8<-- "cpp/analysis.cpp:graph"
```

The call graph, the class hierarchy and the cross-reference index are built on
first use and returned by reference. In C++ the graph works with node and edge
indices: `nodes()` and `edges()` are spans, and `callees`, `callers`,
`dispatch_targets` and `dispatch_sources` return indices into `nodes()`.

The ideas are the same as in Python. The guides explain them:
[code](guide/code.md), [call graph](guide/call-graph.md),
[hierarchy and cross-references](guide/hierarchy-xrefs.md).

## Lifetimes

- `Class`, `Method`, `Field` and `ClassRef` are small handles. Each keeps the
  analysis alive, so a handle stays valid after the `AnalysisContext` it came
  from is destroyed. Copying a handle is cheap.
- A default-constructed handle is a *null* handle. Only `is_valid()` may be
  called on it. `ClassRef::resolve()` returns a null `Class` when the class is
  not loaded.
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
