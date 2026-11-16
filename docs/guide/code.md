# Classes, methods and bytecode

An *analysis context* holds the code: one or more DEX files, loaded together.
Python and Java call it `AnalysisContext`; in Go it is a `*dexpp.Context`. You
load it from an APK, from one DEX file or from several, or you get it from an
opened APK with its `analysis()` method (Go: `Analysis()`).

The examples come in Python, Go and Java. Pick your language in the tabs.

<!-- example: code.load -->

Loading an APK takes every top-level `classes*.dex` entry in multidex order:
`classes.dex`, `classes2.dex`, `classes3.dex`, and so on.

## How the names differ

The three bindings have the same parts, named in the style of each language.
In this guide, a name in the text is the Python one, and the tables give all
three. The rules are simple:

- A Python property, such as `cls.methods`, is a method in Go and Java:
  `cls.Methods()` and `cls.methods()`.
- Python uses `snake_case`, Go uses `CamelCase`, Java uses `camelCase`:
  `find_class`, `FindClass`, `findClass`.
- A lookup that may find nothing returns `None` in Python. In Go it returns
  `nil` when the result is a handle, such as a class, and otherwise a second
  `bool` result that is `false`. In Java it returns an empty `Optional`.

## Classes

<!-- example: code.classes -->

| What | Python | Go | Java |
| ---- | ------ | -- | ---- |
| every class | `ctx.classes()` | `ctx.Classes()` | `ctx.classes()` |
| one class by descriptor | `ctx.find_class(d)` | `ctx.FindClass(d)` | `ctx.findClass(d)` |
| the class's descriptor | `cls.descriptor` | `cls.Descriptor()` | `cls.descriptor()` |
| its package | `cls.package` | `cls.Package()` | `cls.packageName()` |
| its superclass | `cls.superclass` | `cls.Superclass()` | `cls.superclass()` |
| its interfaces | `cls.interfaces` | `cls.Interfaces()` | `cls.interfaces()` |
| its fields | `cls.fields` | `cls.Fields()` | `cls.fields()` |

- `classes()` returns every class of every loaded file, in file order. Each
  call makes a new list with a new class object for every class. On a large
  app that is a lot of objects, so keep the list when you need it more than
  once.
- `find_class(descriptor)` returns one class, or nothing. So the example checks
  the result before it uses it.
- The descriptor of a class is the name the DEX format gives it, such as
  `Lcom/example/Foo;`. The package is in Java source form, `com.example`, the
  same form as the manifest's package name, so the two can be compared
  directly: a class is in the app's package, or below it, when the two are
  equal or the class's package starts with the app's package and a dot. The
  package is an empty string for a class in the default package.
- A class can be defined in more than one loaded file. Then `classes()` lists
  every definition, and `find_class` returns the first one: the definition in
  the earliest file, in load order. Android does the same. An app's class
  loader searches its DEX files in multidex order and stops at the first one
  that defines the class, so a later definition is never loaded. The call
  graph, the class hierarchy and the cross-references also use only the first
  definition.
- The superclass and the interfaces are class *references* (`ClassRef`), not
  classes. The superclass is often a framework class that is not in the file,
  so a reference only carries the descriptor. Call `resolve()` on a reference
  (Go: `Resolve()`) to get the class when it is loaded. When it is not loaded,
  there is no class: `None` in Python, `nil` in Go, an empty `Optional` in
  Java.

## Methods and fields

<!-- example: code.methods -->

A class's methods list the direct methods first (constructors, static and
private methods), then the virtual ones. Constructors are named `<init>`, and
the static initializer is named `<clinit>`.

`has_code` (Go: `HasCode()`, Java: `hasCode()`) is false for abstract and
native methods. For those, the instruction list is empty and the control-flow
graph has no blocks.

The access flags hold one bit per DEX flag, such as `public` or
`constructor`. Each language tests a flag in its own way:

| Language | Type | Test a flag |
| -------- | ---- | ----------- |
| Python | `dexpp.AccessFlags`, an `enum.IntFlag` | `dexpp.AccessFlags.CONSTRUCTOR in flags` |
| Go | `dexpp.AccessFlags`, a `uint32` | `flags.Has(dexpp.AccessConstructor)` |
| Java | `AccessFlags`, a record around an `int` | `flags.has(AccessFlags.CONSTRUCTOR)` |

In all three, the flags are also a plain number: `int(flags)` in Python,
`uint32(flags)` in Go, `flags.value()` in Java. Methods such as `is_public`,
`is_static` and `is_final` (Go: `IsPublic()`, Java: `isPublic()`, and so on)
are shortcuts for single flags.

## Bytecode

A method's instructions are its decoded bytecode. `to_string` (Go:
`dexpp.ToString`, Java: `Dexpp.toString`) renders one instruction as text.

<!-- example: code.instructions -->

The library decodes a method's bytecode once, the first time you ask, and
keeps it. How you read it depends on the language:

- In Python, `method.instructions` is a `dexpp.InstructionView`: a read-only
  sequence that copies nothing up front and builds each instruction when you
  read it. It supports `len()`, indexing, slicing and iteration, like a list.
- In Go, `method.Instructions()` returns a slice, and in Java,
  `method.instructions()` returns a list. Each call copies every instruction
  into new values, so keep the result when you need it more than once. The
  same is true for the control-flow graph below.

The text shows constant-pool operands as raw indices, such as `field@1` or
`string@12`. An instruction alone has no context to turn them into names. The
method has that context: see [The code as text](#the-code-as-text) below.

Each instruction has one of 28 types, one per family of opcodes:
`InvokeInstruction`, `FieldInstruction`, `ConstStringInstruction`,
`IfInstruction` and so on. The names are the same in the three languages. An
opcode the format does not use, as in a damaged method, becomes an
`InvalidInstruction`. To tell the types apart, Python uses `isinstance`, Go a
type switch or a type assertion, and Java pattern matching (`instanceof` or a
`switch`). Every instruction has the fields they all share:

| Field | Python | Go | Java |
| ----- | ------ | -- | ---- |
| the position in the method, in code units | `base.offset` | `Base().Offset` | `offset()` |
| the raw opcode byte | `base.opcode` | `Base().Opcode` | `opcode()` |
| the length of the instruction, in code units | `base.size` | `Base().Size` | `size()` |

`opcode_name` (Go: `dexpp.OpcodeName`, Java: `Dexpp.opcodeName`) gives the
name of an opcode byte.

### The code as text

`disassemble()` (Go: `Disassemble()`, Java: `disassemble()`) returns a
method's code as text, one line per instruction, with every reference named.

<!-- example: code.disassemble -->

Each line has the instruction's `offset` and its `text`. The lines are in the
order of the instructions, so line `i` is the text of instruction `i`. A
method without code has no lines. The text follows the style of smali and
dexdump:

- A method is `Lclass;->name(Params)Return`, a field `Lclass;->name:Type`, a
  type its descriptor, and a prototype `(Params)Return`.
- A string is in double quotes. Quotes, backslashes, control characters (such
  as a newline or a NUL) and invisible characters (such as a zero-width space
  or a right-to-left override) are escaped, so the line shows what is really
  in the string: `"say \"hi\"\n"`, `"\u202e"`.
- A method handle is its kind and its member: `invoke-static@LA;->f()V`. A
  call site (the target of `invoke-custom`) is `call_site@N`, then its method
  name, method type and extra arguments in parentheses, then `@` and the
  bootstrap method.
- Literals are hexadecimal, as in smali: `0x1f`, `-0x1`, and `0x10L` for a
  64-bit constant.
- A branch target is the offset it jumps to, in hexadecimal. It is the same
  number as the `offset` of the line it jumps to, and as the code offsets of
  the rest of the API (cross-references, try blocks, the control-flow graph):
  `if-eqz v0, 0012`.
- Switch and array tables are written inside the instruction that uses them,
  because their data is not an instruction of its own:
  `packed-switch v0, {0x1 -> 0012, 0x2 -> 0016}` and
  `fill-array-data v0, [0x1, 0x2]`. Array elements of 1, 2 and 8 bytes get a
  `t`, `s` and `L` suffix, as in smali.
- The `/range` forms list their registers as `{v0 .. v5}`, and the `/2addr`
  forms name two registers.

In a damaged file, a reference can point outside its table. The line then
shows the raw index followed by a question mark, such as `string@4?`. The
same mark follows a branch target outside the method's code, and a switch or
array table that is missing or damaged.

Each call builds the text again, so keep the list when you need it more than
once.

## What a method refers to

Most analyses do not need raw instructions. They need to know what a method
calls, which fields it touches, which strings it loads and which types it uses.
A method answers these directly, with names already resolved:

<!-- example: code.typed -->

| Python | Go | Java | One entry per | Fields |
| ------ | -- | ---- | ------------- | ------ |
| `calls()` | `Calls()` | `calls()` | invoke instruction | the target (a `MethodId`), the kind, the code offset |
| `field_accesses()` | `FieldAccesses()` | `fieldAccesses()` | field read or write | the field (a `FieldId`), whether it is a write, whether it is static, the code offset |
| `string_loads()` | `StringLoads()` | `stringLoads()` | `const-string` | the value, the code offset |
| `type_uses()` | `TypeUses()` | `typeUses()` | `const-class`, `check-cast`, `instance-of`, `new-instance`, `new-array`, `filled-new-array` | the descriptor, the kind, the code offset |

Each call goes through the method's bytecode again, resolves the names again
and makes a new list. So keep the list when you need it more than once. For
example, do not call `calls()` again for each entry inside a loop.

`calls()` gives the target exactly as the bytecode names it. It does not
follow inheritance or virtual dispatch. The [call graph](call-graph.md) does
that.

These are the forward direction: from a method to what it uses. For the other
direction, from a method, field, string or type to its users, see
[cross-references](hierarchy-xrefs.md#cross-references).

## Registers

Dalvik is a register machine. Each method has a fixed number of registers, and
its arguments arrive in the last ones.

<!-- example: code.registers -->

`register_count` (Go: `RegisterCount()`, Java: `registerCount()`) is the
number of registers. `parameter_registers` (Go: `ParameterRegisters()`, Java:
`parameterRegisters()`) lists the incoming arguments in order. For an instance
method the first entry is `this`. A `long` or `double` argument takes two
registers: `reg` and `reg + 1`, and its `is_wide` is true. Both are 0 or empty
for a method without code.

## Control-flow graph

A method's control-flow graph (Python `method.cfg`, Go `method.Cfg()`, Java
`method.cfg()`) splits the bytecode into basic blocks. A basic block is a run
of instructions that is always executed from its first instruction to its
last.

<!-- example: code.cfg -->

- Blocks are ordered by offset, and block 0 is the entry.
- `first_insn` and `insn_count` (Go: `FirstInsn`, `InsnCount`; Java:
  `firstInsn()`, `insnCount()`) say which entries of the method's instruction
  list belong to the block.
- `successors` are the outgoing edges. An edge has a `target_block` index and
  a `kind`: `FALLTHROUGH`, `GOTO`, `BRANCH_TAKEN`, `BRANCH_NOT_TAKEN`,
  `SWITCH_CASE`, `SWITCH_DEFAULT`, `EXCEPTION` or `EXCEPTION_CATCH_ALL`. In
  Go the kinds are constants such as `dexpp.CfgBranchTaken`, and they print
  in lower case, as `branch-taken`.
- `predecessors` are the indices of the blocks that lead here.
- `is_handler_entry` is true for the first block of a catch handler.

A block with no successors ends the method: it returns or throws.

Two kinds of edge carry one more value. A `SWITCH_CASE` edge has a `case_key`:
the value that case of the switch matches. An `EXCEPTION` edge goes to the
handler of one exception class, and its `catch_type` is the descriptor of that
class. An `EXCEPTION_CATCH_ALL` edge goes to a handler that catches every
exception, which is how Java compiles `finally`, so it has no `catch_type`.
On every other edge both values are absent: `None` in Python, an empty
`Optional` in Java. In Go, `CaseKey` and `CatchType` are plain fields that
hold `0` and `""` on the other edges, so check the kind first.

<!-- example: code.handlers -->

## Try blocks

<!-- example: code.try -->

A try block covers the offsets from `start_offset` up to, but not including,
`end_offset`. `handlers` are its typed `catch` clauses in order.
`catch_all_offset` is the handler that catches everything, which is how
`finally` is compiled. When there is none, it is `None` in Python and an empty
`OptionalInt` in Java; in Go, `HasCatchAll` is false.

## Annotations and initial values

<!-- example: code.annotations -->

Classes, methods and fields each have a list of annotations. An annotation has
a type descriptor, a visibility and its elements. Python gives the elements
as a dictionary from name to value; Go and Java give a list of name and value
pairs, and `Element(name)` (Java: `element(name)`) finds one.

Element values are converted to the plain values of each language:

| Value | Python | Go | Java |
| ----- | ------ | -- | ---- |
| null | `None` | `nil` | `NullValue` |
| boolean | `bool` | `bool` | `BooleanValue` |
| any integer type | `int` | `int64` | `IntValue` |
| `float` or `double` | `float` | `float64` | `FloatValue` |
| string | `str` | `string` | `StringValue` |
| array | `list` | `[]AnnotationValue` | `ArrayValue` |
| nested annotation | `Annotation` | `Annotation` | `Annotation` |

In Java, `AnnotationValue` is a sealed interface and each kind of value is a
record, so record patterns take a value apart, as the example does. In Go,
`AnnotationValue` is `any`, so a type switch tells the kinds apart.

Values that refer to something have their own small types, with the same names
in the three languages: `TypeValue` for a class literal, `EnumValue` for an
enum constant, `FieldValue` for a field, `MethodValue` for a method,
`MethodTypeValue` for a method prototype, and `MethodHandleValue` for a method
handle. A method handle reads or writes a field or calls a method: its kind
is a `MethodHandleKind` such as `STATIC_GET` or `INVOKE_STATIC`, and its
member is the `FieldId` or `MethodId` it works on.

An element that the source left at its default value is **not** in the
elements. The DEX format stores defaults on the annotation's own class, not
at each use.

The initial value of a field (Python `field.initial_value`, Go
`field.InitialValue()`, Java `field.initialValue()`) is the constant a static
field starts with. It is absent when the field has no recorded value, which
means the field starts at its type's default: zero or null.

## All strings

<!-- example: code.strings -->

`strings()` (Go: `Strings()`) returns the whole string table of every loaded
file. It includes names of classes, methods and fields, not only the strings
the code loads. The first call decodes the whole table, and every call copies
all of it into a new list, so it is one of the more expensive calls: keep the
result when you need it more than once. For only the strings that the code
loads, use the `referenced_strings()` of the
[cross-references](hierarchy-xrefs.md#who-loads-a-string).
