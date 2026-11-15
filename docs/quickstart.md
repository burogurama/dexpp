# Quick start

This page walks through the main parts of the API. Every example comes in
three languages: Python, Go and Java. Pick your language in the tabs above an
example. Every other example on the site then switches to the same language.

Every code block on this site is taken from a file that runs in the project's
tests, and the output shown under it is the real output of that code.

To set up a language first, see [Install](install.md) for Python,
[C and Go](c-go.md#go) for Go, and [Java](java.md#build) for Java.

The examples use small files from the repository's `tests/data/` folder. Use
the path of your own APK or DEX file instead. The tests of each language run
in a different folder, so the paths differ: the Python examples open
`tests/data/...`, the Go examples `../../../tests/data/...` and the Java
examples `../../tests/data/...`.

## Open an APK

Opening an APK reads the archive. The manifest, the resources and the signing
information are decoded the first time you ask for each of them.

<!-- example: apk_metadata.manifest -->

More in [APK metadata](guide/apk.md).

## Go from the package to its code

The APK's `analysis()` method (Go: `Analysis()`) returns an *analysis
context*: the object that holds the code of every `classes*.dex` file of the
APK. If you only have a DEX file, or you do not need the manifest, load the
code directly:

<!-- example: code.load -->

| Load | Python | Go | Java |
| ---- | ------ | -- | ---- |
| every `classes*.dex` of an APK | `AnalysisContext.from_apk` | `FromAPK` | `AnalysisContext.fromApk` |
| one DEX file | `AnalysisContext.from_dex` | `FromDEX` | `AnalysisContext.fromDex` |
| several DEX files together | `AnalysisContext.from_dex_files` | `FromDEXFiles` | `AnalysisContext.fromDexFiles` |

## List classes and methods

<!-- example: code.classes -->

Class names and types are written as *descriptors*, the form the DEX format
uses: `Lcom/example/Foo;` for a class, `I` for `int`, `[I` for `int[]`.
[Concepts](guide/concepts.md) explains them.

## Read bytecode

<!-- example: code.instructions -->

More in [Classes, methods and bytecode](guide/code.md).

## Ask who calls what

The call graph covers the whole program. It is built the first time you ask
the analysis context for it.

<!-- example: call_graph.graph call_graph.node -->

More in [Call graph](guide/call-graph.md) and
[Class hierarchy and cross-references](guide/hierarchy-xrefs.md).

## Edit a DEX file

The *model* reads a DEX file into memory, lets you move, copy, remove and
rename whole classes, and writes a valid DEX file again.

<!-- example: edit_dex.read -->

More in [Editing DEX files](guide/editing-dex.md).

## Errors

A file that cannot be used is an error. A lookup that finds nothing is not an
error: it gives an empty result instead.

<!-- example: errors.load -->

Each language reports errors in its own way:

- **Python** raises exceptions. A path that does not exist raises the
  built-in `FileNotFoundError`, and a path that exists but cannot be read,
  such as a directory, raises `OSError`. A file that dexpp cannot use raises
  one of dexpp's own exceptions: `dexpp.InvalidApkError` for a file that is
  not an APK, and `dexpp.InvalidDexError` for a DEX file that cannot be
  parsed. All of dexpp's exceptions have one base class, `dexpp.Error`.
  A lookup that finds nothing returns `None`.
- **Go** returns an `error`. Every failed call returns a `*dexpp.Error`, and
  `errors.Is` tells the kinds apart: `fs.ErrNotExist` or `dexpp.ErrFileNotFound`
  for a missing file, `dexpp.ErrInvalidAPK`, `dexpp.ErrInvalidDex` and so on.
  A lookup that finds nothing returns `nil`, or `false` as its second result.
  See [Errors in Go](c-go.md#errors-in-go).
- **Java** throws exceptions. A file that cannot be opened throws an
  `IOException`, such as `NoSuchFileException`. Every other error is an
  unchecked `DexppException`, with subclasses such as `InvalidApkException`
  and `InvalidDexException`. A lookup that finds nothing returns an empty
  `Optional`. See [Errors](java.md#errors) on the Java page.

[Concepts](guide/concepts.md#errors) has more about errors.
