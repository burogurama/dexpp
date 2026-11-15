# Editing DEX files

`dexpp.Model` is an editable DEX file in memory. You read a file into it,
change it, and write a valid DEX file again.

From Python, the model works on **whole classes**: you can inspect, copy,
move, remove and rename classes. Changing the instructions inside a method, or
building a class from nothing, is only possible from [C++](../cpp.md).

The model is separate from the analysis API. A `Model` is not an
`AnalysisContext`, and a class in a model is not a `dexpp.Class`.

## Read and inspect

```python
--8<-- "edit_dex.py:read"
```

```text
--8<-- "output/edit_dex.read.txt"
```

- `Model.read_file(path)` reads a file. `Model.read(data)` reads `bytes`.
- `Model(version)` makes an empty model.
- `len(model)` is the number of classes, and `descriptor in model` tests for
  a class.
- `class_descriptors()` returns the descriptor of every class, in model
  order. Each call copies them into a new list. Because it is a copy, you can
  loop over it while you move or remove classes, as the merge example below
  does.
- `class_info(descriptor)` returns a small summary, a `ClassInfo`, or `None`
  when the model has no such class. It has the descriptor, the access flags,
  the superclass, the source file, the list of interfaces, and the number of
  fields and methods.

Pass `debug_info=False` to `read` or `read_file` to leave out line numbers and
local variable names. The model is then smaller.

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

```python
--8<-- "edit_dex.py:split"
```

```text
--8<-- "output/edit_dex.split.txt"
```

- `copy_class(src, descriptor)` copies a class from another model and leaves
  the source as it was.
- `move_class(src, descriptor)` takes the class out of the source. The class
  is not copied, so this is cheaper than a copy followed by a remove.
- `remove_class(descriptor)` returns whether a class was removed.

`copy_class` and `move_class` raise `dexpp.ModelError` when the source has no
such class or the destination already has one.

## Merge files

```python
--8<-- "edit_dex.py:merge"
```

```text
--8<-- "output/edit_dex.merge.txt"
```

The output does not depend on the order in which the classes were added. The
writer always puts classes, members and tables in the same fixed order.

## Rename classes

```python
--8<-- "edit_dex.py:rename"
```

```text
--8<-- "output/edit_dex.rename.txt"
```

`rename_types` takes a mapping from old descriptor to new descriptor. It
changes every reference to each class: the class itself, superclasses and
interfaces, method and field identities, array types, instructions, catch
types, annotations, and the class names inside generic signatures.

It does **not** change:

- string constants, so a name passed to `Class.forName("...")` stays as it is;
- Kotlin metadata;
- the simple name stored in an inner class's `InnerClass` annotation.

Rename an inner class together with its outer class.

## Check before writing

```python
--8<-- "edit_dex.py:validate"
```

```text
--8<-- "output/edit_dex.validate.txt"
```

- `validate()` lists every rule of the format the model breaks. Each
  `Problem` has a `where` and a `message`. An empty list means `write` can
  encode the model.
- `pool_stats()` says how many entries each table of the written file would
  have. A DEX file can hold at most 65,535 types and prototypes, and 65,536
  fields and methods. Use this to decide when a file is full and classes must
  go to another one.
- `required_version()` is the lowest DEX version that has every feature the
  model uses. `write` never raises the version by itself. Set `model.version`
  when a higher one is needed.

## Write

```python
--8<-- "edit_dex.py:write"
```

```text
--8<-- "output/edit_dex.write.txt"
```

`write()` returns `bytes`, and `write_file(path)` writes a file. Both check the
model first and write nothing if it is not valid.

Writing is deterministic: the same model always gives the same bytes. Reading
the output and writing it again gives the same bytes too.

Pass `debug_info=False` to strip line numbers and local variable names.

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

The model has its own exceptions. Each one says what to do next.

| Call | Raises | When |
| ---- | ------ | ---- |
| `read`, `read_file` | `dexpp.InvalidDexError` | the input is not a valid DEX file |
| `read`, `read_file` | `dexpp.UnsupportedDexError` | the input uses a format the model does not read, such as the DEX version 41 container |
| `read_file` | `FileNotFoundError` | the file does not exist |
| `read_file` | `OSError` | the file exists but cannot be read |
| `write`, `write_file` | `dexpp.InvalidModelError` | the model breaks rules of the format |
| `write`, `write_file` | `dexpp.PoolOverflowError` | a table would have more entries than the format allows |
| `write`, `write_file` | `dexpp.VersionTooLowError` | a feature needs a higher DEX version than `model.version` |
| `write_file` | `OSError` | the file cannot be created or written |
| `copy_class`, `move_class` | `dexpp.ModelError` | the class is missing in the source or present in the destination |

`InvalidModelError`, `PoolOverflowError` and `VersionTooLowError` derive from
`dexpp.ModelError`, so one `except dexpp.ModelError` handles every failed
write. All of them derive from `dexpp.Error`; see
[Concepts](concepts.md#errors).

### Input that is not a DEX file

```python
--8<-- "edit_dex.py:errors"
```

```text
--8<-- "output/edit_dex.errors.txt"
```

Reading is strict. A file with something the model could not write back
faithfully, such as an unused opcode or a branch into the middle of an
instruction, is refused with `InvalidDexError`.

### A model that is not valid

`InvalidModelError` carries the list of broken rules in `problems`. It is the
list `validate()` returns.

```python
--8<-- "edit_dex.py:invalid"
```

```text
--8<-- "output/edit_dex.invalid.txt"
```

### A table that is too large

`PoolOverflowError` means the classes do not fit in one DEX file. A file holds
at most 65,536 methods and 65,536 fields. Move some classes to another model
and write two files. To avoid the error, check `pool_stats()` while you fill a
model, before you write it.

### A version that is too low

```python
--8<-- "edit_dex.py:version"
```

```text
--8<-- "output/edit_dex.version.txt"
```

## Memory

A model takes about 15 to 25 times the size of its DEX file, because every
reference is held as a name. Reading a 40 MB DEX file needs several hundred
megabytes.
