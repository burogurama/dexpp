# Fuzzing

libFuzzer harnesses over dex++'s untrusted-input parsers. The library exists to
parse adversarial APKs, so these cover the entry points an attacker controls:

| harness              | target                              | seed corpus                     |
| -------------------- | ----------------------------------- | ------------------------------- |
| `fuzz_parse_dex`     | `raw::parser::parse_buffer`         | `tests/data/*.dex`              |
| `fuzz_apk`           | `AnalysisContext::from_apk_buffer`  | `tests/data/*.apk`              |
| `fuzz_axml`          | `axml::parse` (binary manifest)     | `AndroidManifest.xml` from an APK |
| `fuzz_arsc`          | `apk::ResourceTable::parse`         | `resources.arsc` from an APK    |

`fuzz_apk` is the widest: `from_apk_buffer` drives the ZIP reader, multidex
ordering, and DEFLATE inflation, and on a successful parse the harness walks the
lazy analysis surfaces (class data, instructions, CFGs, call graph, hierarchy,
xrefs) — where the analysis layer's "malformed pool index → empty result" access
checks actually live. `fuzz_parse_dex` covers the raw parser and its id-table
accessors directly.

## Build

Requires **Clang ≥ 19** (for libFuzzer). Earlier Clang reports
`__cpp_concepts=201907`, which guards out libstdc++'s `<expected>`; Clang 19
bumped the macro, so the library builds against plain libstdc++ and Clang's
libstdc++-built libFuzzer runtime links cleanly — no libc++ needed.

```bash
# Clang 19 (from apt.llvm.org if your distro ships an older one):
#   wget https://apt.llvm.org/llvm.sh && chmod +x llvm.sh && sudo ./llvm.sh 19
sudo apt-get install -y clang-19 libclang-rt-19-dev

CC=clang-19 CXX=clang++-19 cmake -S . -B build-fuzz \
    -DDEXPP_BUILD_FUZZERS=ON -DDEXPP_BUILD_TESTS=OFF -DDEXPP_BUILD_C=OFF
cmake --build build-fuzz -j
```

Every object is built with `-fsanitize=fuzzer-no-link,address,undefined` so
libFuzzer gets coverage feedback from library code, and ASan/UBSan catch memory
and undefined-behaviour bugs.

## Run

```bash
# extract binary-XML / arsc seeds once
mkdir -p seeds/axml seeds/arsc
unzip -o tests/data/sample.apk AndroidManifest.xml -d seeds/axml
unzip -o tests/data/sample.apk resources.arsc     -d seeds/arsc

./build-fuzz/fuzz_parse_dex tests/data           # DEX seeds are already there
./build-fuzz/fuzz_apk       tests/data
./build-fuzz/fuzz_axml      seeds/axml
./build-fuzz/fuzz_arsc      seeds/arsc
```

The positional argument is a corpus directory libFuzzer both reads seeds from
and writes new inputs to. A crash drops a `crash-<hash>` file in the working
directory; reproduce with `./build-fuzz/<harness> crash-<hash>`.

## CI

`.github/workflows/ci.yml` runs each harness for 60 s on every push/PR (the
`fuzz-smoke` job). `.github/workflows/fuzz-nightly.yml` runs longer sessions on a
schedule, persisting each harness's corpus across runs via the Actions cache and
uploading any crash as an artifact.
