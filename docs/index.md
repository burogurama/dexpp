# dex++

dex++ reads Android APK and DEX files and answers questions about them. It is a
C++23 library with Python, C, Go and Java bindings. The Python package is called
`dexpp`.

This site describes version **{{ dexpp_version }}**. The library is in alpha:
the API may still change between 0.x releases.

## What it does

- **APK metadata.** The manifest (package, SDK levels, permissions, components
  and whether other apps can reach them), string resources, and the signing
  schemes and certificates.
- **Code analysis.** Classes, methods and fields; decoded bytecode; a
  control-flow graph per method; try blocks; annotations; initial values of
  static fields.
- **Whole-program analysis.** A call graph that knows about virtual dispatch,
  the class hierarchy, and a cross-reference index: who calls a method, who
  reads or writes a field, who loads a string, who uses a type.
- **DEX editing.** Read a DEX file into a model, change it, and write a valid
  DEX file again. From Python, Go and Java the model works on whole classes:
  copy, move, remove and rename classes.

It does not decompile to Java, and it does not verify signatures.

## A first look

```python
--8<-- "quickstart.py"
```

```text
--8<-- "output/quickstart.txt"
```

## Where to go next

| You want to | Read |
| ----------- | ---- |
| Install the Python package | [Install](install.md) |
| See the main ideas in ten minutes | [Quick start](quickstart.md) |
| Understand descriptors, handles and laziness | [Concepts](guide/concepts.md) |
| Look up a Python class or method | [Python reference](reference/python/index.md) |
| Use the library from C++ | [C++](cpp.md) and the [C++ reference](reference/cpp.md) |
| Use the library from C or Go | [C and Go](c-go.md) |
| Use the library from Java | [Java](java.md) |

## Speed and memory

dex++ does as little as possible when it loads a file. It maps the file into
memory and parses only the tables it needs. Class data, bytecode, control-flow
graphs, the call graph, the hierarchy and the cross-reference index are each
built the first time you ask for them, and then kept. So loading is cheap, and
you pay only for the analyses you use.
