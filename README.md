# dex++

Fast static analysis of Android APK / DEX files — a C++23 core with Python
bindings, built as a clean, typed replacement for androguard's analysis API.

The current release is **0.2.0** (alpha): the API may still change between
0.x releases. See [CHANGELOG.md](CHANGELOG.md).

## What it does

- **APK metadata** — manifest (package, SDK levels, permissions, components and
  their exported/intent-filter/permission attributes), resource resolution
  (`@string/...`), and signing-scheme/certificate inspection (v1/v2/v3).
- **DEX analysis** — classes, methods, fields; decoded bytecode with mnemonics;
  per-method control-flow graphs and try/catch; annotations and static-field
  initial values.
- **Whole-program** — call graph (with class-hierarchy-aware virtual dispatch),
  class hierarchy, and a cross-reference index (who calls / reads / writes /
  loads what), both forward and reverse.
- **DEX editing** — read a DEX file into an editable model, change it or
  build one from scratch, and write a valid DEX file (`dex::model`). From
  Python (`dexpp.Model`) and Go, the model works a whole class at a time:
  copy, move, remove and rename classes, then write.

It inspects APK and DEX files and edits DEX files. It does not decompile or
verify signatures.

## Install (Python)

dexpp is not on PyPI. Prebuilt wheels are attached to each
[GitHub release](https://github.com/burogurama/dexpp/releases), for CPython
3.9 and newer on:

- Linux x86_64 (glibc 2.28+ and musl)

Install the wheel for your platform straight from the release page:

```bash
pip install dexpp --no-index \
    --find-links https://github.com/burogurama/dexpp/releases/expanded_assets/v0.2.0
```

`--no-index` makes pip take dexpp only from the release page, never from
PyPI. Do not drop it: someone else could publish an unrelated package named
`dexpp` on PyPI.

On other platforms, build from a checkout (needs a C++23 compiler — GCC ≥ 13 or
Clang ≥ 19 — and CMake ≥ 3.18):

```bash
pip install .
```

The package ships type stubs, so editors and `mypy`/`pyright` get full
completion.

```python
import dexpp

apk = dexpp.Apk.open("app.apk")
print(apk.manifest.package, apk.manifest.permissions)
print("signed:", apk.signing.is_signed, [c.sha256_hex for c in apk.signing.certificates])

ctx = apk.analysis()                      # cross into the code
for target, sites in ctx.xrefs.method_refs_by_name("loadUrl"):
    print(target.class_descriptor, len(sites), "call sites")
```

## Build & test (C++)

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure   # run from the project root

./build/main_test tests/data/classes.dex     # demo disassembler
```

The Python module can also be built in-tree for development:

```bash
cmake -S . -B build -DDEXPP_BUILD_PYTHON=ON
cmake --build build -j
PYTHONPATH=build python3 bindings/python/test_dexpp.py
```

## Using it from C++ or Go

The C++ library and the C / Go bindings are source-only for now: there are no
install rules or prebuilt binaries.

- **C++**: add the checkout to your build with `add_subdirectory(dexpp)`, link
  the `dexpp_core` target, and include `dex.hpp`. `DEXPP_VERSION` gives the
  library version.

Editing a DEX file in C++, for example splitting it into one file per class:

```cpp
#include "dex.hpp"

auto input = dex::model::read_file("classes.dex").value();
for (std::size_t i = 0; i < input.classes.size(); ++i) {
    dex::model::Dex one{input.version, {input.classes[i]}};
    if (auto ok = dex::model::write_file(one, "class" + std::to_string(i) + ".dex"); !ok)
        std::cerr << ok.error().message << '\n';
}
```

`write()` builds the file's pools from the references the model holds, so a
class can move to another file as it is. See `src/model/io.hpp` for reading,
writing, `validate()` and `rename_types()`, and `src/model/types.hpp` for the
model itself.
- **Go**: see [bindings/go/README.md](bindings/go/README.md). The cgo package
  links the C shim from this repository's `build/` directory, so it works from
  a checkout, not through `go get`.

## Layout

- `src/raw/` — `dex::raw`: stateless DEX format parser + a minimal ZIP reader,
  and the format-level pieces a writer needs (opcode table, MUTF-8, digests).
- `src/common/` — vocabulary types shared by the layers (`MethodId`, `FieldId`,
  `AccessFlags`).
- `src/analysis/` — `dex`: the handle-based analysis API (the code surface).
- `src/model/` — `dex::model`: the editable DEX model (read, validate, write,
  rename).
- `src/apk/` — `dex::apk`: package metadata (manifest, resources, signing).
- `src/dex.hpp` — the single public umbrella header.
- `bindings/python/` — the pybind11 module, stubs, and Python tests.
- `bindings/c/`, `bindings/go/` — the C ABI shim and the Go package over it.
