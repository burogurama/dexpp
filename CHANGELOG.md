# Changelog

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
  checks, allocation limits against oversized counts and zip bombs), and are
  fuzzed in CI.
