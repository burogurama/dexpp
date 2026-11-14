# C++ and C reference

The C++ and C API reference is generated from the headers with Doxygen. It
lists every public class, struct and function, with the comments from the
source.

[Open the C++ and C reference](cpp-api/index.html){ .md-button .md-button--primary }

Good places to start:

| Name | What it is |
| ---- | ---------- |
| [`dex::AnalysisContext`](cpp-api/classdex_1_1AnalysisContext.html) | Loads DEX files and gives access to classes and the whole-program analyses |
| [`dex::apk::Apk`](cpp-api/classdex_1_1apk_1_1Apk.html) | An APK: archive entries, manifest, resources, signing |
| [`dex::CallGraph`](cpp-api/classdex_1_1CallGraph.html) | The whole-program call graph |
| [`dex::model::Dex`](cpp-api/structdex_1_1model_1_1Dex.html) | The editable model of one DEX file |
| [`model/io.hpp`](cpp-api/io_8hpp.html) | `read`, `write`, `validate`, `rename_types` |
| [`dexpp.h`](cpp-api/dexpp_8h.html) | The C API for analysis |
| [`dexpp_model.h`](cpp-api/dexpp__model_8h.html) | The C API for the editable model |

For how to build and use the library, see [C++](../cpp.md) and
[C and Go](../c-go.md).
