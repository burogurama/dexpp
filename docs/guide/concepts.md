# Concepts

A few ideas appear on every page of this site. This page explains them once.
They are the same in every language; where a name differs, the page gives
the Python, Go and Java forms.

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
with `;`. Every function that takes a class wants it in this form, and a class
gives its own descriptor (Python `cls.descriptor`, Go `cls.Descriptor()`,
Java `cls.descriptor()`). A class has no other name in dex++: method and field
names are simple names, such as `toString`, but a class is always named by its
descriptor. So looking up `Lcom/example/Foo;` finds the class, and looking up
`com.example.Foo` finds nothing.

A method's parameter and return types are written together as a *prototype*:
the parameter descriptors inside parentheses, with nothing between them, then
the return type. `String format(int, long)` has the prototype
`(IJ)Ljava/lang/String;`.

## Method and field identities

A method is identified by three strings: the class it belongs to, its name, and
its prototype. A `MethodId` holds them (Go: `MethodID`). A field is identified
by its class, its name and its type, and a `FieldId` holds those (Go:
`FieldID`).

| Language | A method identity |
| -------- | ----------------- |
| Python | `dexpp.MethodId("LAnimal;", "sound", "()Ljava/lang/String;")` |
| Go | `dexpp.MethodID{ClassDescriptor: "LAnimal;", Name: "sound", Proto: "()Ljava/lang/String;"}` |
| Java | `new MethodId("LAnimal;", "sound", "()Ljava/lang/String;")` |

The prototype is part of the identity so that overloads stay apart:
`print(I)V` and `print(J)V` are two different methods.

Identities are plain values. They compare by content, they can be keys of a
dictionary or a map, and they exist for methods that are not in the file at
all, such as framework methods the app calls.

## Types and absent values

The Python and Java examples on this site write the type of every variable.
The Python package ships type stubs, so a type checker such as mypy checks
those types. The Go examples use `:=`, as Go code usually does; the compiler
still knows every type.

A lookup that may find nothing, or an attribute that may be missing, gives an
*absent* value. Each language writes it in its own way:

| Language | Absent value | How the examples check it |
| -------- | ------------ | ------------------------- |
| Python | `None`. The type says so: `find_class` is typed to return a `dexpp.Class` or `None`. | `assert value is not None` |
| Go | `nil` for a handle, such as the `*dexpp.Class` of `FindClass`. A lookup that returns a plain value also returns a `bool`, which is `false`. A struct field that may be absent is a pointer, such as `Manifest.VersionName`. | `if cls == nil`, `if !ok`, `if p != nil` |
| Java | An empty `Optional`, `OptionalInt` or `OptionalLong`, never `null`. | `orElseThrow()`, `isPresent()`, `ifPresent(...)` |

The examples stop when a value they need is absent. In your own code, handle
that case in the way that fits: return early, report an error, or skip the
item.

## Handles keep the analysis alive

Classes, methods, fields and class references are small *handles*. Each one
points into an analysis context and keeps that context alive. So a class still
works after the context it came from is gone:

<!-- example: concepts.handles -->

The same is true for the call graph, the hierarchy and the cross-reference
index.

In Python you never free anything yourself. In Go and Java the handles hold
memory outside the garbage-collected heap. The garbage collector frees them,
but you can free one at once: `Close()` in Go, `close()` or
`try`-with-resources in Java. Closing a context does not break the handles
that came from it, as the example shows. See
[Handles and memory in Go](../c-go.md#handles-and-memory-in-go) and
[Handles and memory](../java.md#handles-and-memory) on the Java page.

## Everything is built on first use

Loading a file does very little work. The expensive parts are built the first
time you ask for them, and then cached in the context:

| You ask for | What is built, once |
| ----------- | ------------------- |
| a class's methods or fields | that class's member list |
| a method's instructions | that method's decoded bytecode |
| a method's control-flow graph | that method's control-flow graph |
| the call graph | the call graph of the whole program |
| the class hierarchy | the hierarchy of the whole program |
| the cross-references | the cross-reference index of the whole program |

The first request for a whole-program structure can take a moment on a large
app. Every later request returns the cached one.

- In **Python**, everything in this table is a property, such as
  `cls.methods` or `ctx.call_graph`: it is cheap, or built once and kept. Work
  that is done again on every call is a method instead, such as
  `ctx.strings()`, which copies every string of every DEX file into a new
  list, and `ctx.classes()`, which makes a new `Class` object for every class.
- In **Go and Java**, every access is a method call. The library still builds
  each thing once, but most calls copy the result into new Go or Java values.
  So keep a result, such as a method's instructions or the list of classes,
  when you need it more than once.

## Resolved and external

An app refers to many classes and methods it does not contain: the Android
framework, the Java library. dex++ only sees what is in the files you loaded.

- A class reference (`ClassRef`) names a class by descriptor. Resolving it
  (`resolve()`, Go: `Resolve()`) gives the class when the class is in a loaded
  file, and nothing when it is not.
- A call-graph node has the kind `DEFINED` when the method is defined in a
  loaded file. A node for a framework method has the kind `EXTERNAL`, and it
  has no method behind it.
- The class hierarchy's `is_loaded(descriptor)` (Go: `IsLoaded`, Java:
  `isLoaded`) tells you which side a class is on.

## Offsets

Positions inside a method are *code-unit offsets*. Dalvik bytecode is a
sequence of 16-bit units, and an instruction is one to five units long. An
offset counts units from the start of the method, so it is not a byte offset
and not an instruction index. Call sites, cross-references, branch targets and
try ranges all use these offsets, so they can be compared with each other.

## Errors

dex++ reports an error when it cannot use what it was given: a file that does
not exist or cannot be read, a file that is not an APK or a DEX file, or a
model that cannot be written. The kinds are the same in every language. A
lookup that finds nothing is not an error: it gives an absent value.

<!-- example: errors.base -->

**Python** raises exceptions. dexpp's own exceptions all derive from
`dexpp.Error`:

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
when any of them means the same to you. Two more rules:

- **`dexpp.Error` is a `ValueError`.** Code that catches `ValueError` catches
  dexpp's exceptions too.
- **File problems are not dexpp errors.** A path that does not exist raises the
  built-in `FileNotFoundError`. Other file problems, such as a path that is a
  directory or a file that cannot be written, raise `OSError`.
  `FileNotFoundError` is a kind of `OSError`, so `except OSError` covers both.

**Go** returns an `error`. Every failed call returns a `*dexpp.Error`, with a
`Code` that says what went wrong. Each code has a sentinel value, such as
`dexpp.ErrInvalidDex`, so `errors.Is` tells the kinds apart, and
`errors.Is(err, fs.ErrNotExist)` is true for a missing file. Use `errors.As`
to read the code. See [Errors in Go](../c-go.md#errors-in-go) for the list.

**Java** throws exceptions. A file that cannot be opened throws one of the
file exceptions of the JDK, which are `IOException`s, such as
`NoSuchFileException`. Every other error is an unchecked `DexppException`, and
`code()` gives its kind. Its subclasses follow the Python exceptions:
`InvalidApkException`, `InvalidDexException`, `UnsupportedDexException` and
`ModelException`, which has the subclasses `InvalidModelException`,
`PoolOverflowException` and `VersionTooLowException`. See
[Errors](../java.md#errors) on the Java page for the list.

The [editing guide](editing-dex.md#errors) shows the model's errors.

## Malformed input

dex++ is made to read files that may be broken on purpose. A file that is not a
valid archive or DEX file is refused when it is loaded, with an invalid APK or
an invalid DEX error. Damage deeper in the file, such as an index that points
outside a table, does not cause an error later. The affected lookup returns an
empty result instead: an empty string, an empty list, or an absent value.

## Threads

Reading from one analysis context on several threads at the same time is
safe, including the first read that builds a cached structure. In Go, the same
is true for several goroutines.

Loading an APK can use several threads. By default dex++ picks the number of
threads by itself: it uses them to decompress the DEX files of a large APK,
and later to build the call graph. Ask for one thread when your own program
already analyses several APKs in parallel:

| Language | Load an APK on the calling thread only |
| -------- | -------------------------------------- |
| Python | `AnalysisContext.from_apk(path, threads=1)` |
| Go | `dexpp.FromAPK(path, dexpp.WithThreads(1))` |
| Java | `AnalysisContext.fromApk(path, 1)` |

A thread count of 0 is the default: the library chooses.
