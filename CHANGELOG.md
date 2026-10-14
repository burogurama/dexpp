# Changelog

## Unreleased

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
  checks, allocation limits against oversized counts and zip bombs), and are
  fuzzed in CI.
