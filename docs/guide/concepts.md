# Concepts

A few ideas appear on every page of this site. This page explains them once.

## Descriptors

The DEX format names types with *descriptors*, and dex++ uses them as they
are. It does not convert them to Java source names.

| Java | Descriptor |
| ---- | ---------- |
| `com.example.Foo` | `Lcom/example/Foo;` |
| `com.example.Foo.Inner` | `Lcom/example/Foo$Inner;` |
| `int`, `long`, `boolean`, `void` | `I`, `J`, `Z`, `V` |
| `byte`, `short`, `char`, `float`, `double` | `B`, `S`, `C`, `F`, `D` |
| `int[]`, `String[][]` | `[I`, `[[Ljava/lang/String;` |

A class descriptor starts with `L`, uses `/` between package parts, and ends
with `;`. Every function that takes a class name wants this form:

```python
found: dexpp.Class | None = ctx.find_class("Lcom/example/Foo;")   # the class
nothing: dexpp.Class | None = ctx.find_class("com.example.Foo")   # None
```

A method's parameter and return types are written together as a *prototype*:
the parameter descriptors inside parentheses, with nothing between them, then
the return type. `String format(int, long)` has the prototype
`(IJ)Ljava/lang/String;`.

## Method and field identities

A method is identified by three strings: the class it belongs to, its name, and
its prototype. `MethodId` holds them. A field is identified by its class, its
name and its type, and `FieldId` holds those.

```python
sound: dexpp.MethodId = dexpp.MethodId("LAnimal;", "sound", "()Ljava/lang/String;")
out: dexpp.FieldId = dexpp.FieldId("Ljava/lang/System;", "out", "Ljava/io/PrintStream;")
```

The prototype is part of the identity so that overloads stay apart:
`print(I)V` and `print(J)V` are two different methods.

Identities are plain values. They compare by content, they can be dictionary
keys, and they exist for methods that are not in the file at all, such as
framework methods the app calls.

## Types and `None`

The package ships type stubs, and the examples on this site write the type of
every variable:

```python
ctx: dexpp.AnalysisContext = dexpp.AnalysisContext.from_dex("classes.dex")
```

A lookup that may find nothing has `None` in its type. `find_class` returns
`dexpp.Class | None`, and `apk.manifest` is `dexpp.Manifest | None`. The
examples check such a result before they use it, with
`assert value is not None`. A type checker asks for the same check. In your
own code, handle the `None` case in the way that fits: return early, raise, or
skip the item.

## Handles keep the analysis alive

`Class`, `Method`, `Field` and `ClassRef` are small handles. Each one points
into an `AnalysisContext` and keeps that context alive. So this is safe:

```python
def main_class(path: str) -> dexpp.Class | None:
    ctx: dexpp.AnalysisContext = dexpp.AnalysisContext.from_dex(path)
    return ctx.find_class("LMain;")      # ctx goes out of scope here


cls: dexpp.Class | None = main_class("classes.dex")
assert cls is not None
print(cls.methods)                       # still works
```

The same is true for the call graph, the hierarchy, the cross-reference index
and the views they return.

## Everything is built on first use

Loading a file does very little work. The expensive parts are built the first
time you ask for them, and then cached in the context:

| You read | What is built, once |
| -------- | ------------------- |
| `cls.methods`, `cls.fields` | that class's member list |
| `method.instructions` | that method's decoded bytecode |
| `method.cfg` | that method's control-flow graph |
| `ctx.call_graph` | the call graph of the whole program |
| `ctx.class_hierarchy` | the hierarchy of the whole program |
| `ctx.xrefs` | the cross-reference index of the whole program |
| `ctx.strings` | every string of every DEX file, decoded |

The first read of a whole-program structure can take a moment on a large app.
Every later read returns the cached object.

## Resolved and external

An app refers to many classes and methods it does not contain: the Android
framework, the Java library. dex++ only sees what is in the files you loaded.

- A `ClassRef` names a class by descriptor. `resolve()` returns the `Class`
  when the class is in a loaded file, and `None` when it is not.
- A call-graph node has `resolved == True` when the method is defined in a
  loaded file. A node for a framework method has `resolved == False`, and its
  `method` is `None`.
- `ClassHierarchy.is_loaded(descriptor)` tells you which side a class is on.

## Offsets

Positions inside a method are *code-unit offsets*. Dalvik bytecode is a
sequence of 16-bit units, and an instruction is one to five units long. An
offset counts units from the start of the method, so it is not a byte offset
and not an instruction index. Call sites, cross-references, branch targets and
try ranges all use these offsets, so they can be compared with each other.

## Errors

dexpp raises its own exceptions when it cannot use what it was given. They all
derive from `dexpp.Error`:

```text
dexpp.Error
├── InvalidApkError        not a ZIP archive, or an APK with no classes.dex
├── InvalidDexError        a DEX file that cannot be parsed
├── UnsupportedDexError    a DEX format the editable model does not read
└── ModelError             an operation on a Model failed
    ├── InvalidModelError      the model breaks rules of the format
    ├── PoolOverflowError      a table would have too many entries
    └── VersionTooLowError     a feature needs a higher DEX version
```

Catch the specific class when you want to react to one case, and `dexpp.Error`
when any of them means the same to you:

```python
--8<-- "errors.py:base"
```

```text
--8<-- "output/errors.base.txt"
```

Three more rules:

- **`dexpp.Error` is a `ValueError`.** Code that catches `ValueError` catches
  dexpp's exceptions too.
- **File problems are not dexpp errors.** A path that does not exist raises the
  built-in `FileNotFoundError`. Other file problems, such as a path that is a
  directory or a file that cannot be written, raise `OSError`. `FileNotFoundError` is a kind of `OSError`, so
  `except OSError` covers both.
- **Not found is not an error.** A lookup that finds nothing returns `None`.

The [editing guide](editing-dex.md#errors) shows the model's exceptions.

## Malformed input

dex++ is made to read files that may be broken on purpose. A file that is not a
valid archive or DEX file is refused when it is loaded, with
`dexpp.InvalidApkError` or `dexpp.InvalidDexError`. Damage deeper in the file,
such as an index that points outside a table, does not raise later. The
affected lookup returns an empty result instead: an empty string, an empty
list, or `None`.

## Threads

Reading from one `AnalysisContext` on several threads at the same time is safe,
including the first read that builds a cached structure.

`AnalysisContext.from_apk` takes a `threads` argument. With the default, `0`,
dex++ picks the number of threads by itself: it uses them to decompress the DEX
files of a large APK, and later to build the call graph. Pass `threads=1` when
your own program already analyses several APKs in parallel.
