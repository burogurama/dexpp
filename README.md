# dex++

Fast static analysis of Android APK / DEX files — a C++23 core with Python
bindings, built as a clean, typed replacement for androguard's analysis API.

This project is in early development; the API may change.

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

It is an *analysis* library: it inspects, it does not decompile or verify
signatures.

## Install (Python)

```bash
pip install .
```

This builds the native extension (needs a C++23 compiler and CMake ≥ 3.18) and
ships type stubs, so editors and `mypy`/`pyright` get full
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

Coming from androguard? See [DOCS/androguard_migration.md](DOCS/androguard_migration.md).

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

## Layout

- `src/raw/` — `dex::raw`: stateless DEX format parser + a minimal ZIP reader.
- `src/analysis/` — `dex`: the handle-based analysis API (the code surface).
- `src/apk/` — `dex::apk`: package metadata (manifest, resources, signing).
- `src/dex.hpp` — the single public umbrella header.
- `bindings/python/` — the pybind11 module, stubs, and Python tests.

See [CLAUDE.md](CLAUDE.md) for the architecture in depth.
