# Classes, methods and bytecode

An `AnalysisContext` holds the code: one or more DEX files, loaded together.

```python
# One of:
ctx: dexpp.AnalysisContext = dexpp.AnalysisContext.from_dex("classes.dex")
ctx: dexpp.AnalysisContext = dexpp.AnalysisContext.from_dex_files(["classes.dex", "classes2.dex"])
ctx: dexpp.AnalysisContext = dexpp.AnalysisContext.from_apk("app.apk")
ctx: dexpp.AnalysisContext = dexpp.Apk.open("app.apk").analysis()
```

`from_apk` loads every top-level `classes*.dex` entry of the APK in multidex
order: `classes.dex`, `classes2.dex`, `classes3.dex`, and so on.

## Classes

```python
--8<-- "code.py:classes"
```

```text
--8<-- "output/code.classes.txt"
```

- `ctx.classes` lists every class of every loaded file, in file order.
- `ctx.find_class(descriptor)` returns one class, or `None`. `dexpp.Class | None`
  is its type, so the example checks the result before it uses it.
- `cls.superclass` and `cls.interfaces` are `ClassRef` values, not classes.
  The superclass is often a framework class that is not in the file, so a
  reference only carries the descriptor. Call `ref.resolve()` to get the
  `Class` when it is loaded; it returns `None` when it is not.

## Methods and fields

```python
--8<-- "code.py:methods"
```

```text
--8<-- "output/code.methods.txt"
```

`cls.methods` lists the direct methods first (constructors, static and private
methods), then the virtual ones. Constructors are named `<init>`, and the
static initializer is named `<clinit>`.

`method.has_code` is false for abstract and native methods. For those,
`instructions` is empty and `cfg` is an empty graph.

`access_flags` is a `dexpp.AccessFlags`, an `enum.IntFlag` with one member per
DEX flag. Test a flag with `in`, as the example does. It is also an `int`, so
code that compares bits by value keeps working. The `is_public`, `is_static`,
`is_final` and similar properties are shortcuts for single flags.

## Bytecode

`method.instructions` is the decoded bytecode. It is a `dexpp.InstructionView`:
a read-only sequence that copies nothing up front and builds each instruction
when you read it. It supports `len()`, indexing, slicing and iteration, like a
list. `dexpp.to_string` renders one instruction as text.

```python
--8<-- "code.py:instructions"
```

```text
--8<-- "output/code.instructions.txt"
```

`to_string` shows constant-pool operands as raw indices, such as `field@1` or
`string@12`. It has no context to turn them into names. To get names, use the
resolved views below.

Each instruction is an object of one of 24 classes, one per family of opcodes:
`InvokeInstruction`, `FieldInstruction`, `ConstStringInstruction`,
`IfInstruction` and so on. Use `isinstance` to tell them apart. Every one has a
`base` with the fields they all share:

- `base.offset`: the position in the method, in code units;
- `base.opcode`: the raw opcode byte (`dexpp.opcode_name` gives its name);
- `base.size`: the length of the instruction in code units.

## What a method refers to

Most analyses do not need raw instructions. They need to know what a method
calls, which fields it touches, which strings it loads and which types it uses.
A method answers these directly, with names already resolved:

```python
--8<-- "code.py:typed"
```

```text
--8<-- "output/code.typed.txt"
```

| Property | One entry per | Fields |
| -------- | ------------- | ------ |
| `calls` | invoke instruction | `target` (a `MethodId`), `kind`, `code_offset` |
| `field_accesses` | field read or write | `field` (a `FieldId`), `is_write`, `is_static`, `code_offset` |
| `string_loads` | `const-string` | `value`, `code_offset` |
| `type_uses` | `const-class`, `check-cast`, `instance-of`, `new-instance`, `new-array`, `filled-new-array` | `descriptor`, `kind`, `code_offset` |

`calls` gives the target exactly as the bytecode names it. It does not follow
inheritance or virtual dispatch. The [call graph](call-graph.md) does that.

These are the forward direction: from a method to what it uses. For the other
direction, from a method, field, string or type to its users, see
[cross-references](hierarchy-xrefs.md#cross-references).

## Registers

Dalvik is a register machine. Each method has a fixed number of registers, and
its arguments arrive in the last ones.

```python
--8<-- "code.py:registers"
```

```text
--8<-- "output/code.registers.txt"
```

`parameter_registers` lists the incoming arguments in order. For an instance
method the first entry is `this`. A `long` or `double` argument takes two
registers: `reg` and `reg + 1`, and its `is_wide` is true.

## Control-flow graph

`method.cfg` splits the bytecode into basic blocks. A basic block is a run of
instructions that is always executed from its first instruction to its last.

```python
--8<-- "code.py:cfg"
```

```text
--8<-- "output/code.cfg.txt"
```

- Blocks are ordered by offset, and block 0 is the entry.
- `block.first_insn` and `block.insn_count` say which entries of
  `method.instructions` belong to the block.
- `block.successors` are the outgoing edges. An edge has a `target_block`
  index and a `kind`: `FALLTHROUGH`, `GOTO`, `BRANCH_TAKEN`,
  `BRANCH_NOT_TAKEN`, `SWITCH_CASE`, `SWITCH_DEFAULT` or `EXCEPTION`.
- `block.predecessors` are the indices of the blocks that lead here.
- `block.is_handler_entry` is true for the first block of a catch handler.

A block with no successors ends the method: it returns or throws.

## Try blocks

```python
--8<-- "code.py:try"
```

```text
--8<-- "output/code.try.txt"
```

A try block covers the offsets from `start_offset` up to, but not including,
`end_offset`. `handlers` are its typed `catch` clauses in order.
`catch_all_offset` is the handler that catches everything, which is how
`finally` is compiled; it is `None` when there is none.

## Annotations and initial values

```python
--8<-- "code.py:annotations"
```

```text
--8<-- "output/code.annotations.txt"
```

Classes, methods and fields each have an `annotations` list. An annotation has
a `type_descriptor`, a `visibility` and its `elements` as a dictionary.

Element values are converted to plain Python values: `None`, `bool`, `int`,
`float`, `str`, and `list` for arrays. Values that refer to something have
their own small classes: `TypeRef` for a class literal, `EnumRef` for an enum
constant, `FieldRef`, `MethodRef`, `MethodTypeRef` and `MethodHandleRef`. A
nested annotation is an `Annotation`.

An element that the source left at its default value is **not** in the
dictionary. The DEX format stores defaults on the annotation's own class, not
at each use.

`field.initial_value` is the constant a static field starts with. It is `None`
when the field has no recorded value, which means the field starts at its
type's default: zero or null.

## All strings

```python
--8<-- "code.py:strings"
```

```text
--8<-- "output/code.strings.txt"
```

`ctx.strings` is the whole string table of every loaded file. It includes
names of classes, methods and fields, not only the strings the code loads. It
decodes the whole table, so it is one of the more expensive calls. For only
the strings that the code loads, use `ctx.xrefs.referenced_strings`.
