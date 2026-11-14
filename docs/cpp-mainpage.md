# dex++ C++ and C API reference

This is the reference for the C++ library and the C ABI shim, generated from
the headers. The guides, the install steps and the Python reference are on the
[main documentation site](../../index.html).

## C++

Include the single public header:

```cpp
#include "dex.hpp"
```

| Namespace | What it holds | Start at |
| --------- | ------------- | -------- |
| `dex` | Code analysis: classes, methods, bytecode, control-flow graphs, call graph, class hierarchy, cross-references | dex::AnalysisContext |
| `dex::apk` | Package metadata: archive entries, manifest, resources, signing | dex::apk::Apk |
| `dex::axml` | The binary XML decoder behind the manifest | dex::axml::XmlDocument |
| `dex::model` | The editable DEX model: read, change, validate, write | dex::model::Dex and the functions in model/io.hpp |

`DEXPP_VERSION` (from `version.hpp`) gives the library version.

## C

Two headers, built into the static library `libdexpp_c.a`:

- `dexpp.h` — the analysis API: loading, the manifest, classes, methods, the
  string pool.
- `dexpp_model.h` — the editable model, a whole class at a time.
