# Install

## Python

dexpp needs **Python 3.14 or newer**.

```bash
pip install dexpp
```

PyPI has prebuilt wheels for CPython 3.14 on Linux x86_64, for both glibc
(2.28 or newer) and musl. The same wheels are attached to each
[GitHub release](https://github.com/burogurama/dexpp/releases).

On any other platform, pip builds dexpp from the source distribution. That
needs:

- a C++23 compiler: GCC 13 or newer, or Clang 19 or newer;
- CMake 3.18 or newer.

The build downloads two dependencies by itself (pybind11 and libdeflate), so it
needs network access.

Check the install:

```python
import dexpp
print(dexpp.__version__)
```

The package ships type stubs. Editors, `mypy` and `pyright` get full
completion and type checking with no extra package. The examples on this site
write the type of every variable, and they pass `mypy --strict`.

## From a checkout

`pip install .` in a checkout builds and installs the same package.

To work on the library itself, build the module in the tree instead and point
`PYTHONPATH` at the build folder:

```bash
cmake -S . -B build -DDEXPP_BUILD_PYTHON=ON
cmake --build build -j
PYTHONPATH=build python3 bindings/python/test_dexpp.py
```

Run the tests from the repository root. They open their fixture files by
relative path.

## C++, C and Go

The C++ library and the C and Go bindings are used from source. There are no
install rules and no prebuilt binaries yet. See [C++](cpp.md) and
[C and Go](c-go.md).
