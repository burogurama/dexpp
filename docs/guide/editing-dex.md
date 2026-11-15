# Editing DEX files

The *model* is an editable DEX file in memory: `dexpp.Model` in Python,
`*dexpp.Model` in Go, `Model` in Java. You read a file into it, change it,
and write a valid DEX file again.

From Python, Go and Java, the model works on **whole classes**: you can
inspect, copy, move, remove and rename classes. Changing the instructions
inside a method, or building a class from nothing, is only possible from
[C++](../cpp.md).

The model is separate from the analysis API. A model is not an analysis
context, and a class in a model is not a class handle of the analysis API.

The examples come in Python, Go and Java. Pick your language in the tabs.

## Read and inspect

<!-- example: edit_dex.read -->

| What | Python | Go | Java |
| ---- | ------ | -- | ---- |
| read a file | `Model.read_file(path)` | `dexpp.ReadModelFile(path, true)` | `Model.readFile(path)` |
| read bytes | `Model.read(data)` | `dexpp.ReadModel(data, true)` | `Model.read(data)` |
| make an empty model | `Model(version)` | `dexpp.NewModel(version)` | `new Model(version)` |
| the number of classes | `len(model)` | `model.ClassCount()` | `model.classCount()` |
| whether a class is there | `descriptor in model` | `model.HasClass(d)` | `model.hasClass(d)` |
| every class descriptor | `model.class_descriptors()` | `model.ClassDescriptors()` | `model.classDescriptors()` |
| a summary of one class | `model.class_info(d)` | `model.ClassInfo(d)` | `model.classInfo(d)` |

- The class descriptors come in model order. Each call copies them into a new
  list. Because it is a copy, you can loop over it while you move or remove
  classes, as the merge example below does.
- The summary of a class, a `ClassInfo`, has the descriptor, the access flags,
  the superclass, the source file, the list of interfaces, and the number of
  fields and methods. When the model has no such class, there is no summary:
  `None` in Python, `false` as the second result in Go, an empty `Optional` in
  Java.

Reading can leave out the debug information: line numbers and local variable
names. The model is then smaller. In Python, pass `debug_info=False`. In Go,
pass `false` as the second argument of `ReadModelFile` or `ReadModel`. In
Java, use the forms with a second `boolean debugInfo` argument, such as
`Model.readFile(path, false)`.

In Go, a model is a handle, like the other handles of the package: the
garbage collector frees it, and `Close()` frees it at once. In Java, `Model`
is `AutoCloseable`, so `try`-with-resources frees it at the end of a block.

## Why classes can move freely

A DEX file stores every string, type, method and field once, in sorted tables,
and the code refers to them by index. So a class in a file is tied to that
file's tables.

The model holds no indices and no file offsets. Every reference is kept as a
name, and branches point at labels. When you write the model, dex++ collects
all the references, builds and sorts the tables, and picks the encoding of each
instruction. This is why a class can be taken out of one model and put into
another with no extra work.

## Split a file

<!-- example: edit_dex.split -->

| What | Python | Go | Java |
| ---- | ------ | -- | ---- |
| copy a class from another model | `copy_class(src, d)` | `CopyClass(src, d)` | `copyClass(src, d)` |
| take a class out of another model | `move_class(src, d)` | `MoveClass(src, d)` | `moveClass(src, d)` |
| remove a class | `remove_class(d)` | `RemoveClass(d)` | `removeClass(d)` |

- Copying a class leaves the source as it was.
- Moving takes the class out of the source. The class is not copied, so this
  is cheaper than a copy followed by a remove.
- Removing returns whether a class was removed.

Copying and moving fail when the source has no such class or the destination
already has one: Python raises `dexpp.ModelError`, Go returns an error that
matches `dexpp.ErrModel`, and Java throws a `ModelException`.

## Merge files

<!-- example: edit_dex.merge -->

The output does not depend on the order in which the classes were added. The
writer always puts classes, members and tables in the same fixed order.

## Rename classes

<!-- example: edit_dex.rename -->

`rename_types` (Go: `RenameTypes`, Java: `renameTypes`) takes a mapping from
old descriptor to new descriptor. It changes every reference to each class:
the class itself, superclasses and interfaces, method and field identities,
array types, instructions, catch types, annotations, and the class names
inside generic signatures.

It does **not** change:

- string constants, so a name passed to `Class.forName("...")` stays as it is;
- Kotlin metadata;
- the simple name stored in an inner class's `InnerClass` annotation.

Rename an inner class together with its outer class.

## Check before writing

<!-- example: edit_dex.validate -->

- `validate()` (Go: `Validate()`) lists every rule of the format the model
  breaks. Each `Problem` has a `where` and a `message` (Go: `Where` and
  `Message`). An empty list means the model can be written.
- `pool_stats()` (Go: `PoolStats()`, Java: `poolStats()`) says how many
  entries each table of the written file would have. A DEX file can hold at
  most 65,535 types and prototypes, and 65,536 fields and methods. Use this to
  decide when a file is full and classes must go to another one.
- `required_version()` (Go: `RequiredVersion()`, Java: `requiredVersion()`)
  is the lowest DEX version that has every feature the model uses. Writing
  never raises the version by itself. Set the model's version when a higher
  one is needed: `model.version = 37` in Python, `model.SetVersion(37)` in Go,
  `model.setVersion(37)` in Java.

## Write

<!-- example: edit_dex.write -->

`write()` (Go: `Write`) returns the bytes of a DEX file, and `write_file(path)`
(Go: `WriteFile`, Java: `writeFile`) writes a file. Both check the model first
and write nothing if it is not valid.

Writing is deterministic: the same model always gives the same bytes. Reading
the output and writing it again gives the same bytes too.

Writing can strip line numbers and local variable names. In Python, pass
`debug_info=False`. In Go, pass `false` to `Write` or as the second argument
of `WriteFile`. In Java, use `write(false)` or `writeFile(path, false)`.

## What changes in a round trip

Reading a file and writing it back gives a file with the same program, but not
always the same bytes as the input. The writer chooses its own layout and
encodings.

A few things are not kept:

- hidden-API flags, which only exist in boot class path files;
- the link section;
- debug positions that point past the end of a method's code;
- data that no instruction refers to.

## Errors

The model has its own errors. Each one says what to do next.

| Call | Failure | Python raises | Go error matches | Java throws |
| ---- | ------- | ------------- | ---------------- | ----------- |
| read | the input is not a valid DEX file | `dexpp.InvalidDexError` | `dexpp.ErrInvalidDex` | `InvalidDexException` |
| read | the input uses a format the model does not read, such as the DEX version 41 container | `dexpp.UnsupportedDexError` | `dexpp.ErrUnsupportedDex` | `UnsupportedDexException` |
| read a file | the file does not exist | `FileNotFoundError` | `dexpp.ErrFileNotFound` | `NoSuchFileException` |
| read a file | the file exists but cannot be read | `OSError` | `dexpp.ErrCannotRead` | `FileSystemException` |
| write | the model breaks rules of the format | `dexpp.InvalidModelError` | `dexpp.ErrInvalidModel` | `InvalidModelException` |
| write | a table would have more entries than the format allows | `dexpp.PoolOverflowError` | `dexpp.ErrPoolOverflow` | `PoolOverflowException` |
| write | a feature needs a higher DEX version than the model's version | `dexpp.VersionTooLowError` | `dexpp.ErrVersionTooLow` | `VersionTooLowException` |
| write a file | the file cannot be created or written | `OSError` | `dexpp.ErrCannotWrite` | `FileSystemException` |
| copy or move a class | the class is missing in the source or present in the destination | `dexpp.ModelError` | `dexpp.ErrModel` | `ModelException` |

In Python and Java, the errors of a failed write (an invalid model, a table
overflow, a version that is too low) are subclasses of the model error:
`dexpp.ModelError` in Python, `ModelException` in Java. So one `except` or
`catch` handles every failed write. In Go, compare with the values in the
table through `errors.Is`, or read the `Code` of the `*dexpp.Error` with
`errors.As`.

See [Concepts](concepts.md#errors) for errors in general,
[Errors in Go](../c-go.md#errors-in-go), and [Errors](../java.md#errors) on
the Java page.

### Input that is not a DEX file

<!-- example: edit_dex.errors -->

Reading is strict. A file with something the model could not write back
faithfully, such as an unused opcode or a branch into the middle of an
instruction, is refused as an invalid DEX file.

### A model that is not valid

The error for an invalid model carries the list of broken rules: `problems`
in Python, `Problems` of the `*dexpp.Error` in Go, `problems()` in Java. It is
the list `validate()` returns.

<!-- example: edit_dex.invalid -->

### A table that is too large

A table overflow means the classes do not fit in one DEX file. A file holds at
most 65,536 methods and 65,536 fields. Move some classes to another model and
write two files. To avoid the error, check the pool statistics while you fill
a model, before you write it.

### A version that is too low

<!-- example: edit_dex.version -->

## Memory

A model takes about 15 to 25 times the size of its DEX file, because every
reference is held as a name. Reading a 40 MB DEX file needs several hundred
megabytes.
