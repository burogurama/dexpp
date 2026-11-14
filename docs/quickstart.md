# Quick start

This page walks through the main parts of the Python API. Every code block on
this site is taken from a file that runs in the project's tests, and the output
shown under it is the real output.

The examples use small files from the repository's `tests/data/` folder. Use
the path of your own APK or DEX file instead.

## Open an APK

`Apk.open` reads the archive. The manifest, the resources and the signing
information are decoded the first time you ask for each of them.

```python
--8<-- "apk_metadata.py:manifest"
```

```text
--8<-- "output/apk_metadata.manifest.txt"
```

More in [APK metadata](guide/apk.md).

## Go from the package to its code

`apk.analysis()` returns an `AnalysisContext`. It holds every `classes*.dex`
file of the APK. If you only have a DEX file, or you do not need the manifest,
load the code directly:

```python
# One of:
ctx: dexpp.AnalysisContext = dexpp.AnalysisContext.from_apk("app.apk")
ctx: dexpp.AnalysisContext = dexpp.AnalysisContext.from_dex("classes.dex")
ctx: dexpp.AnalysisContext = dexpp.AnalysisContext.from_dex_files(["classes.dex", "classes2.dex"])
```

## List classes and methods

```python
--8<-- "code.py:classes"
```

```text
--8<-- "output/code.classes.txt"
```

Class names and types are written as *descriptors*, the form the DEX format
uses: `Lcom/example/Foo;` for a class, `I` for `int`, `[I` for `int[]`.
[Concepts](guide/concepts.md) explains them.

## Read bytecode

```python
--8<-- "code.py:instructions"
```

```text
--8<-- "output/code.instructions.txt"
```

More in [Classes, methods and bytecode](guide/code.md).

## Ask who calls what

The call graph covers the whole program. It is built the first time you read
`ctx.call_graph`.

```python
--8<-- "call_graph.py:graph"
--8<-- "call_graph.py:node"
```

```text
--8<-- "output/call_graph.graph.txt"
--8<-- "output/call_graph.node.txt"
```

More in [Call graph](guide/call-graph.md) and
[Class hierarchy and cross-references](guide/hierarchy-xrefs.md).

## Edit a DEX file

`Model` reads a DEX file into memory, lets you move, copy, remove and rename
whole classes, and writes a valid DEX file again.

```python
--8<-- "edit_dex.py:read"
```

```text
--8<-- "output/edit_dex.read.txt"
```

More in [Editing DEX files](guide/editing-dex.md).

## Errors

A path that does not exist raises the built-in `FileNotFoundError`. A path
that exists but cannot be read, such as a directory, raises `OSError`. A file
that dexpp cannot use raises one of dexpp's own exceptions:
`dexpp.InvalidApkError` for a file that is not an APK, and
`dexpp.InvalidDexError` for a DEX file that cannot be parsed.

```python
--8<-- "errors.py:load"
```

```text
--8<-- "output/errors.load.txt"
```

All of dexpp's exceptions have one base class, `dexpp.Error`.
[Concepts](guide/concepts.md#errors) lists them.

Lookups that find nothing return `None`. They do not raise.
