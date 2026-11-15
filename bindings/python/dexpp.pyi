"""Type stubs for dexpp — static analysis of Android APK / DEX files."""

from collections.abc import Iterator, Sequence
from enum import Enum, IntFlag
from typing import Optional, Union, overload

from _typeshed import ReadableBuffer

__version__: str

# ===== free functions =====

def opcode_name(opcode: int) -> str:
    """The name of a Dalvik opcode byte, such as `invoke-virtual` for `0x6e`.

    An opcode the format does not use gives `unused-XX`, with the byte in hex.
    """
def to_string(instruction: Instruction) -> str:
    """One instruction as text: the opcode name, then its operands.

    Operands that point into a table of the DEX file are shown as raw indices, such
    as `string@12` or `method@4`. An instruction alone cannot turn them into names.
    For names, use `Method.calls()`, `Method.field_accesses()`, `Method.string_loads()`
    and `Method.type_uses()`.
    """

# ===== errors =====

class Error(ValueError):
    """The base class of the errors dexpp raises when it cannot use its input.

    Catch it to handle every such error in one place. It is a `ValueError`, so code
    that catches `ValueError` works too.

    A path that cannot be opened is not a `dexpp.Error`. It raises the built-in
    `FileNotFoundError`, or `OSError` for other file problems.
    """

class InvalidApkError(Error):
    """The file is not a ZIP archive, or the APK has no `classes.dex`."""

class InvalidDexError(Error):
    """A DEX file cannot be parsed: it is not a DEX file, or it is damaged."""

class UnsupportedDexError(Error):
    """A DEX file uses a format the editable model does not support, such as the
    version 41 container format.
    """

class ModelError(Error):
    """An operation on a `Model` failed.

    It is the base class of the errors `Model.write` raises. `Model.copy_class` and
    `Model.move_class` raise it directly.
    """

class InvalidModelError(ModelError):
    """A `Model` breaks rules of the DEX format, so it cannot be written.

    `problems` lists every broken rule. `Model.validate` returns the same list
    without raising.
    """

    def __init__(self, message: str = ..., problems: Sequence[Problem] = ...) -> None:
        """Make the error.

        Args:
            message: A summary of what is wrong.
            problems: The broken rules.
        """
    @property
    def problems(self) -> list[Problem]:
        """Every rule of the DEX format the model breaks, with its location."""

class PoolOverflowError(ModelError):
    """A table of the written DEX file would have more entries than the format
    allows, for example more than 65,536 methods.

    Move classes to another model and write two files. `Model.pool_stats()` gives the
    table sizes before writing.
    """

class VersionTooLowError(ModelError):
    """The `Model` uses a feature that its DEX version does not have.

    Set `Model.version` to `Model.required_version()`, or higher, and write again.
    """

# ===== enums =====

class AccessFlags(IntFlag):
    """The access flags of a class, a method or a field, as the DEX format stores them.

    It is an `enum.IntFlag`, so it is also an `int`: code that tests bits by value
    keeps working. Test a flag with `in`:
    `AccessFlags.PUBLIC in cls.access_flags`.

    Two pairs of flags share a bit, because the DEX format gives them the same
    value: `VOLATILE` and `BRIDGE` (0x40), and `TRANSIENT` and `VARARGS` (0x80).
    The first of each pair is for fields, the second for methods. In Python the
    second name is an alias of the first, so `AccessFlags.BRIDGE is
    AccessFlags.VOLATILE`.
    """

    PUBLIC = 0x0001
    """`public`."""
    PRIVATE = 0x0002
    """`private`."""
    PROTECTED = 0x0004
    """`protected`."""
    STATIC = 0x0008
    """`static`."""
    FINAL = 0x0010
    """`final`."""
    SYNCHRONIZED = 0x0020
    """`synchronized`, on a method."""
    VOLATILE = 0x0040
    """`volatile`, on a field. The same bit as `BRIDGE`."""
    BRIDGE = 0x0040
    """A bridge method the compiler made. The same bit as `VOLATILE`."""
    TRANSIENT = 0x0080
    """`transient`, on a field. The same bit as `VARARGS`."""
    VARARGS = 0x0080
    """A method whose last parameter is `...`. The same bit as `TRANSIENT`."""
    NATIVE = 0x0100
    """`native`, on a method: its code is not in the DEX file."""
    INTERFACE = 0x0200
    """The class is an interface."""
    ABSTRACT = 0x0400
    """`abstract`."""
    STRICT = 0x0800
    """`strictfp`, on a method."""
    SYNTHETIC = 0x1000
    """Made by the compiler; not in the source code."""
    ANNOTATION = 0x2000
    """The class is an annotation type."""
    ENUM = 0x4000
    """The class is an enum, or the field is one of its constants."""
    CONSTRUCTOR = 0x10000
    """A constructor, `<init>`, or the static initializer, `<clinit>`."""
    DECLARED_SYNCHRONIZED = 0x20000
    """A method declared `synchronized`."""

class InvokeKind(Enum):
    """The kind of invoke instruction at a call site."""

    VIRTUAL = 0
    """`invoke-virtual`: a normal call on an object. The method that runs depends on the
    class of the object.
    """
    SUPER = 1
    """`invoke-super`: a call to the superclass's version of a method."""
    DIRECT = 2
    """`invoke-direct`: a call to a constructor or a private method."""
    STATIC = 3
    """`invoke-static`: a call to a static method."""
    INTERFACE = 4
    """`invoke-interface`: a call through an interface type."""
    POLYMORPHIC = 5
    """`invoke-polymorphic`: a call through a method handle, such as
    `MethodHandle.invoke`.
    """
    CUSTOM = 6
    """`invoke-custom`: a call site that is bound at runtime. Compilers emit it for
    lambdas and for string concatenation.
    """

class EdgeOrigin(Enum):
    """How a call-graph edge came to point at its callee."""

    DECLARED = 0
    """The target exactly as the bytecode names it. Every call site has exactly one edge
    of this kind.
    """
    INHERITED_RESOLUTION = 1
    """The class the bytecode names does not define the method itself. The edge points
    at the nearest definition in a loaded superclass or interface, which is the
    method Android finds at runtime. A call site has at most one edge of this kind.
    """

class CfgEdgeKind(Enum):
    """The kind of edge between two basic blocks of a control-flow graph."""

    FALLTHROUGH = 0
    """Execution runs on into the next block. The block's last instruction is not a
    branch, a return or a throw.
    """
    GOTO = 1
    """An unconditional `goto`."""
    BRANCH_TAKEN = 2
    """The condition of an `if` instruction holds, and it jumps."""
    BRANCH_NOT_TAKEN = 3
    """The condition of an `if` instruction does not hold, and execution continues after
    it.
    """
    SWITCH_CASE = 4
    """One case of a switch. The edge's `label` is the case key."""
    SWITCH_DEFAULT = 5
    """No case of a switch matches, and execution continues after it."""
    EXCEPTION = 6
    """From a block inside a try range to a catch handler. The edge's `label` is the
    index of the caught type in the DEX file's type table, or 4294967295
    (`0xFFFFFFFF`) for a handler that catches everything.
    """

class TypeUseKind(Enum):
    """What an instruction does with the type it names."""

    CONST_CLASS = 0
    """`const-class`: loads the class object, as `Foo.class` does."""
    CHECK_CAST = 1
    """`check-cast`: a cast to the type."""
    INSTANCE_OF = 2
    """`instance-of`: an `instanceof` test against the type."""
    NEW_INSTANCE = 3
    """`new-instance`: creates an object of the type."""
    NEW_ARRAY = 4
    """`new-array`: creates an array. The descriptor is the array type, such as `[I`."""
    FILLED_NEW_ARRAY = 5
    """`filled-new-array`: creates an array and fills it from registers. The descriptor
    is the array type.
    """

class OperandWidth(Enum):
    """How many registers a value takes."""

    SINGLE = 0
    """A 32-bit value or a reference. It takes one register."""
    PAIR = 1
    """A 64-bit value, `long` or `double`. It takes two registers, `vN` and `vN+1`."""

class AnnotationVisibility(Enum):
    """Who can see an annotation."""

    BUILD = 0
    """Only tools that read the file. The annotation is not visible to the running app.
    `@Override` is an example in source code.
    """
    RUNTIME = 1
    """The running app can read the annotation through reflection."""
    SYSTEM = 2
    """Data the compiler writes for the runtime in `dalvik.annotation.*` annotations:
    generic signatures, default values, inner-class information.
    """

class ComponentKind(Enum):
    """The kind of a component declared in the manifest."""

    ACTIVITY = 0
    """An `<activity>`: a screen of the app."""
    SERVICE = 1
    """A `<service>`: work that runs without a screen."""
    RECEIVER = 2
    """A `<receiver>`: a broadcast receiver."""
    PROVIDER = 3
    """A `<provider>`: a content provider."""

# ===== identities =====

class MethodId:
    """The identity of a method: its class, its name and its prototype.

    It is a plain value. Two identities with the same three strings are equal, and an
    identity can be a dictionary key or a set member. An identity also exists for a
    method that is not in the loaded files, such as a framework method.

    The prototype is part of the identity, so overloads of one name are different
    methods.
    """

    def __init__(self, class_descriptor: str, name: str, proto: str) -> None:
        """Make an identity.

        Args:
            class_descriptor: The class, such as `Ljava/lang/Object;`.
            name: The method name, such as `toString` or `<init>`.
            proto: The prototype, such as `()Ljava/lang/String;`.
        """
    @property
    def class_descriptor(self) -> str:
        """Descriptor of the class the method belongs to, such as `Ljava/lang/Object;`.
        """
    @property
    def name(self) -> str:
        """The method name, such as `toString`. A constructor is `<init>`."""
    @property
    def proto(self) -> str:
        """The prototype: the parameter types inside parentheses with nothing between
        them, then the return type. `String f(int, long)` is
        `(IJ)Ljava/lang/String;`.
        """
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...

class FieldId:
    """The identity of a field: its class, its name and its type.

    It is a plain value. Two identities with the same three strings are equal, and an
    identity can be a dictionary key or a set member.
    """

    def __init__(self, class_descriptor: str, name: str, type: str) -> None:
        """Make an identity.

        Args:
            class_descriptor: The class, such as `Ljava/lang/System;`.
            name: The field name, such as `out`.
            type: Descriptor of the field's type, such as `Ljava/io/PrintStream;`.
        """
    @property
    def class_descriptor(self) -> str:
        """Descriptor of the class the field belongs to."""
    @property
    def name(self) -> str:
        """The field name."""
    @property
    def type(self) -> str:
        """Descriptor of the field's type."""
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...

# ===== annotations =====

class TypeRef:
    """A class literal as an annotation value, such as `String.class`.

    It is a plain value: two are equal when their content is equal, and it can be a
    dictionary key or a set member.
    """

    @property
    def descriptor(self) -> str:
        """Descriptor of the type."""
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...

class EnumRef:
    """An enum constant as an annotation value.

    It is a plain value: two are equal when their content is equal, and it can be a
    dictionary key or a set member.
    """

    @property
    def constant(self) -> FieldId:
        """The static field that holds the enum constant."""
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...

class FieldRef:
    """A reference to a field as an annotation value. It is rare outside system
    annotations.

    It is a plain value: two are equal when their content is equal, and it can be a
    dictionary key or a set member.
    """

    @property
    def field(self) -> FieldId:
        """The field."""
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...

class MethodRef:
    """A reference to a method as an annotation value. It is rare outside system
    annotations.

    It is a plain value: two are equal when their content is equal, and it can be a
    dictionary key or a set member.
    """

    @property
    def method(self) -> MethodId:
        """The method."""
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...

class MethodTypeRef:
    """A method prototype as an annotation value.

    It is a plain value: two are equal when their content is equal, and it can be a
    dictionary key or a set member.
    """

    @property
    def proto(self) -> str:
        """The prototype, such as `(I)V`."""
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...

class MethodHandleRef:
    """A method handle as an annotation value.

    It is a plain value: two are equal when their content is equal, and it can be a
    dictionary key or a set member.
    """

    @property
    def index(self) -> int:
        """Index of the handle in the DEX file's method handle table. It is not resolved
        further.
        """
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...

AnnotationValue = Union[
    None, bool, int, float, str,
    TypeRef, EnumRef, FieldRef, MethodRef, MethodTypeRef, MethodHandleRef,
    list["AnnotationValue"], "Annotation",
]
"""The value of an annotation element, and the type of `Field.initial_value`.

Numbers, booleans, strings and `null` become plain Python values. All integer types
(`byte`, `short`, `char`, `int`, `long`) become `int`, and `float` and `double`
become `float`, so the original width is not kept. An array becomes a `list`. A value
that refers to something is one of the small reference classes, and a nested
annotation is an `Annotation`.
"""

class Annotation:
    """One annotation on a class, a method or a field.

    An annotation is a copy of the data. It stays valid on its own.
    """

    @property
    def type_descriptor(self) -> str:
        """Descriptor of the annotation's class, such as `Ljava/lang/Deprecated;`."""
    @property
    def visibility(self) -> AnnotationVisibility:
        """Who can see the annotation."""
    @property
    def elements(self) -> dict[str, AnnotationValue]:
        """The elements the source set, by name.

        An element left at its default value is not in the dictionary. The DEX format
        stores defaults once, on the annotation's own class.
        """

# ===== instructions =====

class InstructionBase:
    """The fields every instruction has."""

    @property
    def offset(self) -> int:
        """Position of the instruction in the method, in 16-bit code units from the
        start of the code.
        """
    @property
    def opcode(self) -> int:
        """The raw opcode byte. `opcode_name` gives its name."""
    @property
    def size(self) -> int:
        """Length of the instruction in code units, from 1 to 5."""

class NopInstruction:
    """`nop`: does nothing."""

    base: InstructionBase
    """The fields every instruction has: `offset`, `opcode` and `size`."""

class GotoInstruction:
    """`goto`, `goto/16`, `goto/32`: an unconditional jump."""

    base: InstructionBase
    """The fields every instruction has: `offset`, `opcode` and `size`."""
    branch_offset: int
    """Where the branch goes, in code units, counted from this instruction's own offset.
    It is negative for a jump backwards.
    """

class IfInstruction:
    """`if-eq`, `if-ne`, `if-lt`, `if-ge`, `if-gt`, `if-le`: compare two registers and
    jump when the condition holds.
    """

    base: InstructionBase
    """The fields every instruction has: `offset`, `opcode` and `size`."""
    branch_offset: int
    """Where the branch goes, in code units, counted from this instruction's own offset.
    It is negative for a jump backwards.
    """
    reg_a: int
    """The first register compared."""
    reg_b: int
    """The second register compared."""

class IfZeroInstruction:
    """`if-eqz`, `if-nez`, `if-ltz`, `if-gez`, `if-gtz`, `if-lez`: compare one register
    with zero and jump when the condition holds.
    """

    base: InstructionBase
    """The fields every instruction has: `offset`, `opcode` and `size`."""
    branch_offset: int
    """Where the branch goes, in code units, counted from this instruction's own offset.
    It is negative for a jump backwards.
    """
    reg: int
    """The register compared with zero."""

class SwitchCase:
    """One case of a switch instruction."""

    key: int
    """The value the case matches."""
    target: int
    """Where the case jumps, in code units, counted from the offset of the switch
    instruction.
    """

class SwitchInstruction:
    """`packed-switch`, `sparse-switch`: jump to the case that matches a register."""

    base: InstructionBase
    """The fields every instruction has: `offset`, `opcode` and `size`."""
    payload_offset: int
    """Where the case table is stored, in code units, counted from this instruction's
    own offset.
    """
    reg: int
    """The register that is tested."""
    is_packed: bool
    """True for `packed-switch`, false for `sparse-switch`."""
    cases: list[SwitchCase]
    """The decoded cases. The list is empty when the case table is damaged."""

class ReturnInstruction:
    """`return-void`, `return`, `return-wide`, `return-object`."""

    base: InstructionBase
    """The fields every instruction has: `offset`, `opcode` and `size`."""
    reg: Optional[int]
    """The register that holds the returned value, or `None` for `return-void`."""
    width: OperandWidth
    """The width of the returned value."""

class ThrowInstruction:
    """`throw`: throws the exception held in a register."""

    base: InstructionBase
    """The fields every instruction has: `offset`, `opcode` and `size`."""
    reg: int
    """The register that holds the exception."""

class MoveInstruction:
    """`move`, `move-wide`, `move-object` and their `/from16` and `/16` forms, the
    `move-result` instructions, and `move-exception`.
    """

    base: InstructionBase
    """The fields every instruction has: `offset`, `opcode` and `size`."""
    dest: int
    """The register written."""
    src: int
    """The register read. It is 0 for the `move-result` instructions and
    `move-exception`, which take their value from the last call or the caught
    exception and have no source register.
    """
    width: OperandWidth
    """The width of the value moved."""

class ConstInstruction:
    """`const/4`, `const/16`, `const`, `const/high16`, `const-wide/16`, `const-wide/32`,
    `const-wide`, `const-wide/high16`: load a number into a register.
    """

    base: InstructionBase
    """The fields every instruction has: `offset`, `opcode` and `size`."""
    dest: int
    """The register written."""
    value: int
    """The number, as a 64-bit integer for every form. The bytecode does not say whether
    a constant is an integer or a floating-point number, so a `float` or `double`
    constant appears here as its bit pattern.
    """
    width: OperandWidth
    """`PAIR` for the `const-wide` forms, `SINGLE` for the others."""

class ConstStringInstruction:
    """`const-string`, `const-string/jumbo`: load a string into a register."""

    base: InstructionBase
    """The fields every instruction has: `offset`, `opcode` and `size`."""
    dest: int
    """The register written."""
    string_index: int
    """Index of the string in the DEX file's string table. `Method.string_loads()`
    gives the string itself.
    """

class ConstClassInstruction:
    """`const-class`: load a class object into a register."""

    base: InstructionBase
    """The fields every instruction has: `offset`, `opcode` and `size`."""
    dest: int
    """The register written."""
    type_index: int
    """Index of the type in the DEX file's type table. `Method.type_uses()` gives
    the descriptor.
    """

class ConstHandleInstruction:
    """`const-method-handle`, `const-method-type`."""

    base: InstructionBase
    """The fields every instruction has: `offset`, `opcode` and `size`."""
    dest: int
    """The register written."""
    index: int
    """Index into the method handle table for `const-method-handle`, or into the
    prototype table for `const-method-type`.
    """

class TypeInstruction:
    """`check-cast`, `instance-of`, `new-instance`, `new-array`."""

    base: InstructionBase
    """The fields every instruction has: `offset`, `opcode` and `size`."""
    type_index: int
    """Index of the type in the DEX file's type table. `Method.type_uses()` gives
    the descriptor.
    """
    dest: int
    """The register the instruction works on: the register that is cast for
    `check-cast`, the result for `instance-of` and `new-array`, the new object for
    `new-instance`.
    """
    src: Optional[int]
    """The object register for `instance-of`, the size register for `new-array`. `None`
    for `check-cast` and `new-instance`, which use one register.
    """

class FilledNewArrayInstruction:
    """`filled-new-array`, `filled-new-array/range`: create an array and fill it from
    registers.
    """

    base: InstructionBase
    """The fields every instruction has: `offset`, `opcode` and `size`."""
    type_index: int
    """Index of the array type in the DEX file's type table."""
    arg_regs: list[int]
    """The registers that hold the elements, in order."""

class MonitorInstruction:
    """`monitor-enter`, `monitor-exit`: take and release the lock of an object. They are
    the start and end of a `synchronized` block.
    """

    base: InstructionBase
    """The fields every instruction has: `offset`, `opcode` and `size`."""
    reg: int
    """The register that holds the object."""
    is_enter: bool
    """True for `monitor-enter`, false for `monitor-exit`."""

class FillArrayInstruction:
    """`fill-array-data`: fill an array from a table of constants stored in the code."""

    base: InstructionBase
    """The fields every instruction has: `offset`, `opcode` and `size`."""
    reg: int
    """The register that holds the array."""
    payload_offset: int
    """Where the table of constants is stored, in code units, counted from this
    instruction's own offset.
    """

class CompareInstruction:
    """`cmpl-float`, `cmpg-float`, `cmpl-double`, `cmpg-double`, `cmp-long`: compare two
    values and store -1, 0 or 1.
    """

    base: InstructionBase
    """The fields every instruction has: `offset`, `opcode` and `size`."""
    dest: int
    """The register that gets the result. The result is always an `int`."""
    src_a: int
    """The first register compared."""
    src_b: int
    """The second register compared."""
    src_width: OperandWidth
    """`PAIR` when the operands are `long` or `double`, `SINGLE` for `float`."""

class ArrayOpInstruction:
    """The `aget` and `aput` instructions: read or write one element of an array."""

    base: InstructionBase
    """The fields every instruction has: `offset`, `opcode` and `size`."""
    value_reg: int
    """The register the element is read into, or written from."""
    array_reg: int
    """The register that holds the array."""
    index_reg: int
    """The register that holds the index."""
    is_write: bool
    """True for the `aput` instructions."""
    width: OperandWidth
    """The width of the element."""

class FieldInstruction:
    """The `iget`, `iput`, `sget` and `sput` instructions: read or write a field."""

    base: InstructionBase
    """The fields every instruction has: `offset`, `opcode` and `size`."""
    field_index: int
    """Index of the field in the DEX file's field table. `Method.field_accesses()`
    gives the `FieldId`.
    """
    value_reg: int
    """The register the field is read into, or written from."""
    object_reg: Optional[int]
    """The register that holds the object. `None` for a static field."""
    is_static: bool
    """True for the `sget` and `sput` instructions."""
    is_write: bool
    """True for the `iput` and `sput` instructions."""
    width: OperandWidth
    """The width of the field's value."""

class InvokeInstruction:
    """`invoke-virtual`, `invoke-super`, `invoke-direct`, `invoke-static`,
    `invoke-interface` and their `/range` forms: call a method.
    """

    base: InstructionBase
    """The fields every instruction has: `offset`, `opcode` and `size`."""
    method_index: int
    """Index of the method in the DEX file's method table. `Method.calls()` gives the
    `MethodId` and the kind of invoke.
    """
    arg_regs: list[int]
    """The argument registers, in order. For a call on an object the first one holds the
    object. A `long` or `double` argument takes two entries.
    """

class InvokeCustomInstruction:
    """`invoke-custom`, `invoke-custom/range`: call through a call site bound at
    runtime.
    """

    base: InstructionBase
    """The fields every instruction has: `offset`, `opcode` and `size`."""
    call_site_index: int
    """Index of the call site in the DEX file's call site table."""
    arg_regs: list[int]
    """The argument registers, in order."""

class UnaryOpInstruction:
    """One-operand arithmetic and conversions: `neg-int`, `not-long`, `int-to-long`,
    `double-to-float` and the like.
    """

    base: InstructionBase
    """The fields every instruction has: `offset`, `opcode` and `size`."""
    dest: int
    """The register written."""
    src: int
    """The register read."""
    dest_width: OperandWidth
    """The width of the result."""
    src_width: OperandWidth
    """The width of the operand."""

class BinaryOpInstruction:
    """Two-operand arithmetic on registers: `add-int`, `sub-long`, `mul-double`,
    `shl-int` and the like, and their `/2addr` forms.
    """

    base: InstructionBase
    """The fields every instruction has: `offset`, `opcode` and `size`."""
    dest: int
    """The register written."""
    src_a: int
    """The first operand. For a `/2addr` form it is the same register as `dest`."""
    src_b: int
    """The second operand."""
    width: OperandWidth
    """The width of the operation."""

class BinaryLitInstruction:
    """Arithmetic on a register and a constant: `add-int/lit16`, `rsub-int`,
    `mul-int/lit8`, `ushr-int/lit8` and the like.
    """

    base: InstructionBase
    """The fields every instruction has: `offset`, `opcode` and `size`."""
    dest: int
    """The register written."""
    src: int
    """The register read."""
    literal: int
    """The constant operand."""

Instruction = Union[
    NopInstruction, GotoInstruction, IfInstruction, IfZeroInstruction,
    SwitchInstruction, ReturnInstruction, ThrowInstruction, MoveInstruction,
    ConstInstruction, ConstStringInstruction, ConstClassInstruction,
    ConstHandleInstruction, TypeInstruction, FilledNewArrayInstruction,
    MonitorInstruction, FillArrayInstruction, CompareInstruction,
    ArrayOpInstruction, FieldInstruction, InvokeInstruction,
    InvokeCustomInstruction, UnaryOpInstruction, BinaryOpInstruction,
    BinaryLitInstruction,
]
"""Any decoded instruction: one of the 24 instruction classes.

Use `isinstance` to find out which one. Every class has a `base` with the offset, the
opcode and the size.
"""

class InstructionView(Sequence[Instruction]):
    """The decoded instructions of one method, as a read-only sequence.

    It copies nothing up front: reading `view[i]` builds that one instruction.
    It supports `len()`, indexing, slicing (which returns a `list`) and
    iteration. The view keeps the analysis alive.
    """

    def __len__(self) -> int: ...
    @overload
    def __getitem__(self, index: int) -> Instruction: ...
    @overload
    def __getitem__(self, index: slice) -> list[Instruction]: ...
    def __iter__(self) -> Iterator[Instruction]: ...

# ===== CFG =====

class CfgEdge:
    """An edge from one basic block to another."""

    @property
    def target_block(self) -> int:
        """Index in `Cfg.blocks` of the block the edge leads to."""
    @property
    def kind(self) -> CfgEdgeKind:
        """Why execution can go along this edge."""
    @property
    def label(self) -> Optional[int]:
        """Extra data for two kinds of edge, `None` for the others. For `SWITCH_CASE` it
        is the case key. For `EXCEPTION` it is the index of the caught type in the
        DEX file's type table, or 4294967295 for a handler that catches everything.
        """

class BasicBlock:
    """A run of instructions that is always entered at its first instruction and left at
    its last one.
    """

    @property
    def index(self) -> int:
        """Position of the block in `Cfg.blocks`."""
    @property
    def start_offset(self) -> int:
        """Offset of the block's first instruction, in code units."""
    @property
    def end_offset(self) -> int:
        """Offset just past the block's last instruction, in code units."""
    @property
    def first_insn(self) -> int:
        """Index in `Method.instructions` of the block's first instruction."""
    @property
    def insn_count(self) -> int:
        """Number of instructions in the block."""
    @property
    def successors(self) -> list[CfgEdge]:
        """The edges that leave the block. A block that returns or throws has none."""
    @property
    def predecessors(self) -> list[int]:
        """Indices in `Cfg.blocks` of the blocks with an edge to this one."""
    @property
    def is_handler_entry(self) -> bool:
        """True when the block is the start of a catch handler."""

class BasicBlockView(Sequence[BasicBlock]):
    """Read-only, zero-copy sequence of a CFG's blocks."""

    def __len__(self) -> int: ...
    @overload
    def __getitem__(self, index: int) -> BasicBlock: ...
    @overload
    def __getitem__(self, index: slice) -> list[BasicBlock]: ...
    def __iter__(self) -> Iterator[BasicBlock]: ...

class Cfg:
    """The control-flow graph of one method.

    The blocks are ordered by offset, and block 0 is where the method starts.
    `len(cfg)` is the number of blocks.
    """

    @property
    def blocks(self) -> BasicBlockView:
        """The basic blocks, as a read-only sequence that copies nothing."""
    @property
    def entry(self) -> Optional[BasicBlock]:
        """The block where the method starts, or `None` when the graph is empty."""
    def block_at_offset(self, offset: int) -> Optional[BasicBlock]:
        """The block that starts at this code-unit offset, or `None` when no block
        starts there. Use it to follow a branch target to its block.
        """
    @property
    def empty(self) -> bool:
        """True when there are no blocks: the method is abstract or native."""
    def __len__(self) -> int: ...

# ===== forward references & try blocks =====

class MethodCall:
    """One call in a method's code."""

    @property
    def code_offset(self) -> int:
        """Offset of the invoke instruction in the method's code, in code units."""
    @property
    def target(self) -> MethodId:
        """The called method, exactly as the bytecode names it. Inheritance and virtual
        dispatch are not applied; the call graph does that.
        """
    @property
    def kind(self) -> InvokeKind:
        """The kind of invoke instruction."""

class FieldAccess:
    """One read or write of a field in a method's code."""

    @property
    def code_offset(self) -> int:
        """Offset of the instruction in the method's code, in code units."""
    @property
    def field(self) -> FieldId:
        """The field that is read or written."""
    @property
    def is_write(self) -> bool:
        """True for a write, false for a read."""
    @property
    def is_static(self) -> bool:
        """True when the instruction is a static field access."""

class StringLoad:
    """One `const-string` instruction in a method's code."""

    @property
    def code_offset(self) -> int:
        """Offset of the instruction in the method's code, in code units."""
    @property
    def value(self) -> str:
        """The string that is loaded."""

class TypeUse:
    """One instruction in a method's code that names a type: `const-class`,
    `check-cast`, `instance-of`, `new-instance`, `new-array` or `filled-new-array`.
    """

    @property
    def code_offset(self) -> int:
        """Offset of the instruction in the method's code, in code units."""
    @property
    def descriptor(self) -> str:
        """Descriptor of the type the instruction names."""
    @property
    def kind(self) -> TypeUseKind:
        """What the instruction does with the type."""

class CatchHandler:
    """One typed `catch` clause of a try block."""

    @property
    def type_descriptor(self) -> str:
        """Descriptor of the exception class the clause catches."""
    @property
    def handler_offset(self) -> int:
        """Offset of the handler's first instruction, in code units."""

class TryBlock:
    """A range of a method's code that is covered by exception handlers."""

    @property
    def start_offset(self) -> int:
        """Offset of the first covered instruction, in code units."""
    @property
    def end_offset(self) -> int:
        """Offset just past the covered range, in code units."""
    @property
    def handlers(self) -> list[CatchHandler]:
        """The typed `catch` clauses, in the order they are tried."""
    @property
    def catch_all_offset(self) -> Optional[int]:
        """Offset of the handler that catches every exception, or `None` when there is
        none. A `finally` block is compiled to such a handler.
        """

class ParameterRegister:
    """An incoming argument of a method and the register it arrives in.

    Dalvik puts the arguments in the last registers of the method's frame. For an
    instance method the implicit `this` comes first, then the declared parameters in
    order.
    """

    @property
    def reg(self) -> int:
        """The register that holds the argument. A wide argument also uses `reg + 1`."""
    @property
    def type(self) -> str:
        """Descriptor of the argument's type. For `this` it is the class that defines
        the method.
        """
    @property
    def is_this(self) -> bool:
        """True for the implicit `this` of an instance method."""
    @property
    def is_wide(self) -> bool:
        """True for `long` and `double`, which take two registers."""

# ===== handles =====

class ClassRef:
    """A reference to a class by its descriptor.

    The class may or may not be in the loaded files. Superclasses and interfaces are
    given as references because they are often framework classes, which an app does
    not contain.

    Two references are equal when they name the same class and come from the same
    analysis, so a reference can be a dictionary key or a set member. A `ClassRef`
    is never equal to a `Class`.
    """

    @property
    def descriptor(self) -> str:
        """Descriptor of the class, such as `Ljava/lang/Object;`."""
    @property
    def is_resolved(self) -> bool:
        """True when the class is defined in a loaded file."""
    def resolve(self) -> Optional[Class]:
        """The `Class`, or `None` when the class is not defined in a loaded file."""
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...

class Field:
    """A field declared by a class in a loaded DEX file.

    Two `Field` objects are equal when they are the same field of the same analysis,
    so a field can be a dictionary key or a set member. The same file loaded twice
    gives two analyses, and their fields are not equal.
    """

    @property
    def name(self) -> str:
        """The field name."""
    @property
    def declaring_class(self) -> ClassRef:
        """The class that declares the field."""
    @property
    def type(self) -> str:
        """Descriptor of the field's type."""
    @property
    def access_flags(self) -> AccessFlags:
        """The access flags. The `is_...` properties read single flags of it."""
    @property
    def is_public(self) -> bool:
        """True when the field is `public`."""
    @property
    def is_static(self) -> bool:
        """True when the field is `static`."""
    @property
    def is_final(self) -> bool:
        """True when the field is `final`."""
    @property
    def annotations(self) -> list[Annotation]:
        """The annotations on the field."""
    @property
    def initial_value(self) -> AnnotationValue:
        """The constant a static field starts with.

        `None` when the field is not static or has no recorded value. The field then
        starts at the default of its type: zero, false or null.
        """
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...

class Method:
    """A method defined by a class in a loaded DEX file.

    Two `Method` objects are equal when they are the same method of the same analysis,
    so a method can be a dictionary key or a set member. The same file loaded twice
    gives two analyses, and their methods are not equal.
    """

    @property
    def name(self) -> str:
        """The method name. A constructor is `<init>`, and the static initializer is
        `<clinit>`.
        """
    @property
    def declaring_class(self) -> ClassRef:
        """The class that defines the method."""
    @property
    def return_type(self) -> str:
        """Descriptor of the return type. `V` means `void`."""
    @property
    def parameters(self) -> list[str]:
        """Descriptors of the declared parameter types, in order. `this` is not
        included.
        """
    @property
    def access_flags(self) -> AccessFlags:
        """The access flags. The `is_...` properties read single flags of it."""
    @property
    def has_code(self) -> bool:
        """True when the method has bytecode. False for abstract and native methods."""
    @property
    def is_constructor(self) -> bool:
        """True for `<init>` and `<clinit>`."""
    @property
    def is_static(self) -> bool:
        """True when the method is `static`."""
    @property
    def is_public(self) -> bool:
        """True when the method is `public`."""
    @property
    def is_private(self) -> bool:
        """True when the method is `private`."""
    @property
    def is_abstract(self) -> bool:
        """True when the method is `abstract`."""
    @property
    def is_native(self) -> bool:
        """True when the method is `native`."""
    @property
    def instructions(self) -> InstructionView:
        """The decoded bytecode, in order, as a read-only sequence. It is empty for a
        method without code.

        The bytecode is decoded once and kept. The sequence copies nothing up front:
        each instruction is built when it is read.
        """
    @property
    def cfg(self) -> Cfg:
        """The control-flow graph. It is built once and kept. Empty for a method without
        code.
        """
    @property
    def register_count(self) -> int:
        """Number of registers in the method's frame. 0 for a method without code."""
    @property
    def parameter_registers(self) -> list[ParameterRegister]:
        """The incoming arguments and the registers they arrive in, with `this` first
        for an instance method. Empty for a method without code.
        """
    def calls(self) -> list[MethodCall]:
        """Every call in the method's code, in order. The targets are as the bytecode
        names them.

        Each call goes through the method's bytecode again and makes a new list, so
        keep the result when you need it more than once.
        """
    def field_accesses(self) -> list[FieldAccess]:
        """Every field read and write in the method's code, in order.

        Each call goes through the method's bytecode again and makes a new list, so
        keep the result when you need it more than once.
        """
    def string_loads(self) -> list[StringLoad]:
        """Every `const-string` in the method's code, in order.

        Each call goes through the method's bytecode again and makes a new list, so
        keep the result when you need it more than once.
        """
    def type_uses(self) -> list[TypeUse]:
        """Every instruction in the method's code that names a type, in order.

        Each call goes through the method's bytecode again and makes a new list, so
        keep the result when you need it more than once.
        """
    @property
    def try_blocks(self) -> list[TryBlock]:
        """The try ranges of the method and their handlers. Empty when it has none."""
    @property
    def annotations(self) -> list[Annotation]:
        """The annotations on the method."""
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...

class Class:
    """A class defined in a loaded DEX file.

    Two `Class` objects are equal when they are the same class of the same analysis,
    so a class can be a dictionary key or a set member. The same file loaded twice
    gives two analyses, and their classes are not equal.
    """

    @property
    def descriptor(self) -> str:
        """The class descriptor, such as `Lcom/example/Foo;`. A class has no simple name
        here: `Method.name` and `Field.name` are simple names, and a class is named by
        its descriptor.
        """
    @property
    def package(self) -> str:
        """The package in Java source form, with `.` between its parts, such as
        `com.example`: the same form as `Manifest.package`, so the two can be
        compared directly. Empty for a class in the default package.
        """
    @property
    def superclass(self) -> Optional[ClassRef]:
        """The direct superclass. `None` only for `java.lang.Object` itself."""
    @property
    def interfaces(self) -> list[ClassRef]:
        """The interfaces the class implements directly, in declaration order."""
    @property
    def methods(self) -> list[Method]:
        """The methods the class defines: first the direct methods (constructors, static
        and private methods), then the virtual methods. Inherited methods are not
        included.
        """
    @property
    def fields(self) -> list[Field]:
        """The fields the class declares: first the static fields, then the instance
        fields. Inherited fields are not included.
        """
    @property
    def annotations(self) -> list[Annotation]:
        """The annotations on the class."""
    @property
    def access_flags(self) -> AccessFlags:
        """The access flags. The `is_...` properties read single flags of it."""
    @property
    def source_file(self) -> Optional[str]:
        """The source file name recorded for the class, such as `Foo.java`, or `None`.
        """
    @property
    def is_public(self) -> bool:
        """True when the class is `public`."""
    @property
    def is_abstract(self) -> bool:
        """True when the class is `abstract`."""
    @property
    def is_interface(self) -> bool:
        """True when the class is an interface."""
    @property
    def is_final(self) -> bool:
        """True when the class is `final`."""
    @property
    def is_enum(self) -> bool:
        """True when the class is an enum."""
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...

# ===== call graph =====

class CallEdge:
    """One call site: an invoke instruction in a caller, pointing at a callee.

    A call site has one `DECLARED` edge, to the target the bytecode names. It has a
    second, `INHERITED_RESOLUTION` edge when the named class does not define the
    method itself. Both edges have the same caller, kind and offset.
    """

    @property
    def caller_node(self) -> int:
        """Index in `CallGraph.nodes` of the calling method."""
    @property
    def callee_node(self) -> int:
        """Index in `CallGraph.nodes` of the called method."""
    @property
    def caller(self) -> CallNode:
        """The calling method's node."""
    @property
    def callee(self) -> CallNode:
        """The called method's node."""
    @property
    def kind(self) -> InvokeKind:
        """The kind of invoke instruction."""
    @property
    def code_offset(self) -> int:
        """Offset of the invoke instruction in the caller's code, in code units."""
    @property
    def origin(self) -> EdgeOrigin:
        """Whether the callee is the declared target or the one found by walking up the
        hierarchy.
        """
    @property
    def dispatch_targets(self) -> CallNodeView:
        """The other methods this call site may run because of virtual dispatch.

        For a `DECLARED` edge of an `invoke-virtual` or `invoke-interface` it is the
        same as `callee.dispatch_targets`. It is empty for every other edge.
        """
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...

class CallNode:
    """A method in the call graph.

    A node is *resolved* when the method is defined in a loaded file. A method the
    app calls but does not contain is a node too, with `resolved` false.
    """

    @property
    def index(self) -> int:
        """Position of the node in `CallGraph.nodes`."""
    @property
    def id(self) -> MethodId:
        """The method's identity."""
    @property
    def resolved(self) -> bool:
        """True when the method is defined in a loaded file."""
    @property
    def outgoing(self) -> list[int]:
        """Indices in `CallGraph.edges` of the call sites in this method. `out_edges`
        gives the edges.
        """
    @property
    def incoming(self) -> list[int]:
        """Indices in `CallGraph.edges` of the calls into this method. `in_edges` gives
        the edges.
        """
    @property
    def out_edges(self) -> CallEdgeView:
        """The call sites in this method, as edges."""
    @property
    def in_edges(self) -> CallEdgeView:
        """The calls into this method, as edges."""
    @property
    def dispatch_targets(self) -> CallNodeView:
        """The other methods a virtual or interface call declared against this method
        may run: the overrides in loaded subclasses and implementers.

        The list is stored once on the node and shared by all its call sites. Every
        target is a resolved node. It is empty when no such call names this method.
        """
    @property
    def dispatch_sources(self) -> CallNodeView:
        """The reverse of `dispatch_targets`: the declared methods through which a
        virtual or interface call can reach this method.
        """
    def callees(self) -> CallNodeView:
        """Every method this method may call: the callee of each outgoing edge, plus the
        dispatch targets of each virtual and interface call. No duplicates, in index
        order.

        Each call collects and sorts the list again, so keep the result when you need
        it more than once.
        """
    def callers(self) -> CallNodeView:
        """Every method that may call this method, directly or through virtual dispatch.
        No duplicates, in index order.

        Each call collects and sorts the list again. For a method that is called from
        many places, that means going through every one of those call sites, so keep
        the result when you need it more than once.
        """
    @property
    def method(self) -> Optional[Method]:
        """The `Method` of a resolved node, or `None` for an external method."""
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...

class CallNodeView(Sequence[CallNode]):
    """A read-only sequence of call-graph nodes that copies nothing: all nodes of a
    graph, or a subset such as the dispatch targets of a node or the result of
    `CallNode.callers()`.

    It supports `len()`, indexing, slicing and iteration.
    """

    def __len__(self) -> int: ...
    @overload
    def __getitem__(self, index: int) -> CallNode: ...
    @overload
    def __getitem__(self, index: slice) -> list[CallNode]: ...
    def __iter__(self) -> Iterator[CallNode]: ...

class CallEdgeView(Sequence[CallEdge]):
    """A read-only sequence of call-graph edges that copies nothing.

    It supports `len()`, indexing, slicing and iteration.
    """

    def __len__(self) -> int: ...
    @overload
    def __getitem__(self, index: int) -> CallEdge: ...
    @overload
    def __getitem__(self, index: slice) -> list[CallEdge]: ...
    def __iter__(self) -> Iterator[CallEdge]: ...
    def where(
        self, *, origin: Optional[EdgeOrigin] = None, kind: Optional[InvokeKind] = None
    ) -> CallEdgeView:
        """The edges of this view that match every given filter.

        The filtering is done inside the library. A large app has millions of call
        sites, so this is much faster than a Python loop.
        `graph.edges.where(origin=EdgeOrigin.DECLARED)` gives exactly one edge per
        call site.

        Args:
            origin: Keep only edges with this origin.
            kind: Keep only edges with this kind of invoke.
        """

class CallGraph:
    """The call graph of the whole program: which method can call which.

    Get it from `AnalysisContext.call_graph`. It is built on the first read and kept.
    """

    @property
    def nodes(self) -> CallNodeView:
        """Every method in the graph, as a read-only sequence that copies nothing."""
    @property
    def edges(self) -> CallEdgeView:
        """Every call-site edge in the graph, as a read-only sequence that copies
        nothing.
        """
    def find(self, id: MethodId) -> Optional[CallNode]:
        """The node of the method with this identity, or `None` when the graph has no
        such method.
        """
    def method_for(self, node: CallNode) -> Optional[Method]:
        """The `Method` of a resolved node, or `None` for an external method. Same as
        `node.method`.
        """
    @property
    def empty(self) -> bool:
        """True when the graph has no nodes."""

# ===== class hierarchy =====

class ClassHierarchy:
    """Inheritance over the whole program.

    Get it from `AnalysisContext.class_hierarchy`. It is built on the first read and
    kept. Every query takes and returns class descriptors.

    A class outside the loaded files is in the hierarchy when a loaded class names it
    as its superclass or as an interface. Its own ancestors are not known. A query
    about a descriptor the hierarchy has never seen gives an empty list or `False`.
    """

    def supertypes(self, descriptor: str) -> list[str]:
        """The direct superclass, then the directly implemented interfaces."""
    def subclasses(self, descriptor: str) -> list[str]:
        """The classes that directly extend this class."""
    def implementers(self, descriptor: str) -> list[str]:
        """The classes that directly implement this interface."""
    def all_supertypes(self, descriptor: str) -> list[str]:
        """Every ancestor: superclasses and interfaces, at any distance. The class
        itself is not included.
        """
    def all_descendants(self, descriptor: str) -> list[str]:
        """Every class below this one, at any distance, through both `extends` and
        `implements`. The class itself is not included.
        """
    def is_subtype_of(self, sub: str, super: str) -> bool:
        """True when `sub` is `super` itself or has `super` among its ancestors."""
    def is_loaded(self, descriptor: str) -> bool:
        """True when the class is defined in a loaded file."""
    def is_interface(self, descriptor: str) -> bool:
        """True when the class is defined in a loaded file and is an interface. False
        for a class outside the loaded files, because its kind is not known.
        """
    @property
    def empty(self) -> bool:
        """True when the hierarchy has no classes."""

# ===== xrefs =====

class StringXref:
    """One `const-string` instruction that loads a string."""

    @property
    def referrer(self) -> int:
        """Index in `Xrefs.referrers` of the method that contains the instruction."""
    @property
    def code_offset(self) -> int:
        """Offset of the instruction in the method's code, in code units."""

class FieldXref:
    """One instruction that reads or writes a field."""

    @property
    def referrer(self) -> int:
        """Index in `Xrefs.referrers` of the method that contains the instruction."""
    @property
    def code_offset(self) -> int:
        """Offset of the instruction in the method's code, in code units."""
    @property
    def is_write(self) -> bool:
        """True for a write, false for a read."""
    @property
    def is_static(self) -> bool:
        """True when the instruction is a static field access."""

class TypeXref:
    """One instruction that names a type."""

    @property
    def referrer(self) -> int:
        """Index in `Xrefs.referrers` of the method that contains the instruction."""
    @property
    def code_offset(self) -> int:
        """Offset of the instruction in the method's code, in code units."""
    @property
    def kind(self) -> TypeUseKind:
        """What the instruction does with the type."""

class MethodXref:
    """One invoke instruction that calls a method."""

    @property
    def referrer(self) -> int:
        """Index in `Xrefs.referrers` of the method that contains the instruction."""
    @property
    def code_offset(self) -> int:
        """Offset of the instruction in the method's code, in code units."""
    @property
    def kind(self) -> InvokeKind:
        """The kind of invoke instruction."""

class ReferrerView(Sequence[MethodId]):
    """A read-only sequence of method identities that copies nothing. The `referrer`
    number of a cross-reference site is an index into it.

    It supports `len()`, indexing, slicing and iteration.
    """

    def __len__(self) -> int: ...
    @overload
    def __getitem__(self, index: int) -> MethodId: ...
    @overload
    def __getitem__(self, index: slice) -> list[MethodId]: ...
    def __iter__(self) -> Iterator[MethodId]: ...

class Xrefs:
    """The cross-reference index of the whole program: who uses a method, a field, a
    string or a type.

    Get it from `AnalysisContext.xrefs`. It is built on the first read and kept.

    Every query returns a list of sites. A site has a `code_offset` and a `referrer`:
    the number of the method whose code contains the instruction. Only instructions
    are indexed.
    """

    @property
    def referrers(self) -> ReferrerView:
        """The identity of every method that contains at least one indexed instruction.
        `referrers[site.referrer]` is the method a site is in.
        """
    def referrer_id(self, referrer: int) -> MethodId:
        """The identity of one referrer. Same as `referrers[referrer]`.

        Raises:
            IndexError: The number is out of range.
        """
    def referrer_method(self, referrer: int) -> Optional[Method]:
        """The `Method` of one referrer."""
    def method_refs(self, id: MethodId) -> list[MethodXref]:
        """The call sites whose target, as the bytecode names it, is exactly this
        method.

        Virtual dispatch is not applied: a call to `Animal.sound` is not listed under
        `Dog.sound`. Use the call graph for that.
        """
    def method_refs_by_name(self, name: str) -> list[tuple[MethodId, list[MethodXref]]]:
        """The call sites whose target has this method name, in any class and with any
        prototype.

        Each item of the result pairs one called method with its call sites. It is
        the quick way to find every call to a name such as `exec` or `loadUrl`.
        """
    def field_refs(self, id: FieldId) -> list[FieldXref]:
        """Every read and write of this field."""
    def field_reads(self, id: FieldId) -> list[FieldXref]:
        """Only the reads of this field."""
    def field_writes(self, id: FieldId) -> list[FieldXref]:
        """Only the writes of this field."""
    def string_refs(self, value: str) -> list[StringXref]:
        """The `const-string` instructions that load exactly this string. To search by
        substring or pattern, filter `referenced_strings()` first.
        """
    def referenced_strings(self) -> list[str]:
        """Every distinct string that some `const-string` instruction loads.

        Each call makes a new list of all of them, so keep the result when you need
        it more than once.
        """
    def type_refs(self, descriptor: str) -> list[TypeXref]:
        """The instructions that name this type: `const-class`, `check-cast`,
        `instance-of`, `new-instance`, `new-array` and `filled-new-array`.
        """
    @property
    def empty(self) -> bool:
        """True when nothing is indexed."""

# ===== analysis context =====

class AnalysisContext:
    """The code of a program: one or more DEX files, loaded together.

    It is the starting point for code analysis. Loading does little work. Class data,
    bytecode and the whole-program structures are built the first time they are read,
    and kept.

    Every object that comes from a context keeps it alive, so you do not have to keep
    the context yourself.
    """

    @staticmethod
    def from_dex(path: str) -> AnalysisContext:
        """Load one DEX file.

        Raises:
            FileNotFoundError: The path does not exist.
            OSError: The path exists but cannot be read, for example a directory.
            InvalidDexError: The file is not a valid DEX file.
        """
    @staticmethod
    def from_dex_files(paths: list[str]) -> AnalysisContext:
        """Load several DEX files into one context, for example the files of a multidex
        app that are already unpacked.

        Raises:
            FileNotFoundError: A path does not exist.
            OSError: A path exists but cannot be read, for example a directory.
            InvalidDexError: A file is not a valid DEX file.
        """
    @staticmethod
    def from_apk(path: str, threads: int = 0) -> AnalysisContext:
        """Load every top-level `classes*.dex` entry of an APK, in multidex order:
        `classes.dex`, `classes2.dex`, `classes3.dex` and so on.

        Args:
            path: The APK file.
            threads: How many threads to use. 0 lets the library choose. 1 keeps all
                work on the calling thread; use it when your program already analyses
                several APKs in parallel. The threads are used to decompress the DEX
                files of a large APK, and later to build the call graph.

        Raises:
            FileNotFoundError: The path does not exist.
            OSError: The path exists but cannot be read, for example a directory.
            InvalidApkError: The file is not a ZIP archive, or it has no `classes.dex`.
            InvalidDexError: One of its DEX files is not valid.
        """
    def classes(self) -> list[Class]:
        """Every class of every loaded DEX file, in file order.

        Each call makes a new list with a new `Class` object for every class, so keep
        the result when you need it more than once. To get one class by its
        descriptor, use `find_class`.
        """
    def strings(self) -> list[str]:
        """Every string of every loaded DEX file's string table.

        This includes names of classes, methods and fields, not only the strings the
        code loads. A string that is in two DEX files appears twice. The first call
        decodes the whole table, and every call copies all of it into a new list, so
        it is one of the more expensive calls: keep the result when you need it more
        than once. For only the strings the code loads, use
        `xrefs.referenced_strings()`.
        """
    def find_class(self, descriptor: str) -> Optional[Class]:
        """The class with this descriptor, such as `Lcom/example/Foo;`, or `None` when
        no loaded file defines it.
        """
    @property
    def call_graph(self) -> CallGraph:
        """The call graph of the whole program. Built on the first read and kept."""
    @property
    def class_hierarchy(self) -> ClassHierarchy:
        """The class hierarchy of the whole program. Built on the first read and kept.
        """
    @property
    def xrefs(self) -> Xrefs:
        """The cross-reference index of the whole program. Built on the first read and
        kept.
        """

# ===== APK layer =====

class IntentFilter:
    """An `<intent-filter>` of a component."""

    @property
    def actions(self) -> list[str]:
        """The `android:name` of each `<action>`."""
    @property
    def categories(self) -> list[str]:
        """The `android:name` of each `<category>`."""
    def has_action(self, name: str) -> bool:
        """True when the filter lists this action."""
    @property
    def is_launcher(self) -> bool:
        """True for the filter that puts an activity in the launcher: action
        `android.intent.action.MAIN` with category
        `android.intent.category.LAUNCHER`.
        """

class Component:
    """An activity, a service, a broadcast receiver or a content provider declared in
    the manifest.
    """

    @property
    def kind(self) -> ComponentKind:
        """Which of the four kinds of component this is."""
    @property
    def name(self) -> str:
        """The component's class name with its package, such as
        `com.example.MainActivity`. A short name in the manifest, such as
        `.MainActivity`, is completed with the app's package.
        """
    @property
    def exported(self) -> Optional[bool]:
        """The `android:exported` attribute as the manifest states it: `True`, `False`,
        or `None` when the attribute is missing. `is_exported` gives the effective
        answer.
        """
    @property
    def permission(self) -> Optional[str]:
        """The permission a caller must hold to use the component, or `None`."""
    @property
    def intent_filters(self) -> list[IntentFilter]:
        """The component's intent filters."""
    @property
    def guarded_by_signature_permission(self) -> bool:
        """True when `permission` is a permission this app declares with the `signature`
        or `signatureOrSystem` protection level. Only apps signed with the same key
        can hold such a permission.
        """
    @property
    def is_exported(self) -> bool:
        """Whether other apps can address the component: the `exported` attribute when
        the manifest sets it, otherwise true when the component has an intent filter.
        """
    @property
    def is_reachable(self) -> bool:
        """Whether an app from another developer can reach the component: it is
        exported, and it is not guarded by a signature-level permission that this app
        declares.

        A component guarded by a normal or dangerous permission stays reachable,
        because any app can request those. So does a component guarded by a
        permission the manifest does not declare, because its protection level is not
        known.
        """

class Manifest:
    """The decoded `AndroidManifest.xml`.

    Attributes the manifest may leave out are `None` when they are not set.
    """

    @property
    def package(self) -> str:
        """The app's package name, from the `package` attribute of `<manifest>`."""
    @property
    def version_code(self) -> Optional[int]:
        """`android:versionCode`, or `None`."""
    @property
    def version_name(self) -> Optional[str]:
        """`android:versionName`, or `None`."""
    @property
    def min_sdk(self) -> Optional[int]:
        """`android:minSdkVersion` of `<uses-sdk>`, or `None`."""
    @property
    def target_sdk(self) -> Optional[int]:
        """`android:targetSdkVersion` of `<uses-sdk>`, or `None`."""
    @property
    def permissions(self) -> list[str]:
        """The permissions the app asks for: the name of each `<uses-permission>`."""
    @property
    def declared_permissions(self) -> list[str]:
        """The permissions the app itself declares: the name of each `<permission>`."""
    def declared_permission_protection_level(self, name: str) -> Optional[int]:
        """The `android:protectionLevel` of a permission the app declares, as the raw
        Android value, or `None` when the app does not declare this permission.

        The low four bits are the base level: 0 normal, 1 dangerous, 2 signature, 3
        signatureOrSystem. Higher bits are flags.
        """
    @property
    def components(self) -> list[Component]:
        """Every activity, service, receiver and provider the manifest declares."""
    def components_of(self, kind: ComponentKind) -> list[Component]:
        """The components of one kind."""
    @property
    def launcher_activity(self) -> Optional[str]:
        """The name of the activity the launcher starts, or `None` when there is none.
        """
    @property
    def debuggable(self) -> Optional[bool]:
        """`android:debuggable` of `<application>`, or `None` when it is not set."""
    @property
    def allow_backup(self) -> Optional[bool]:
        """`android:allowBackup` of `<application>`, or `None` when it is not set."""

class ResourceName:
    """The full name of a resource: `package:type/entry`.

    It is a plain value: two are equal when their content is equal, and it can be a
    dictionary key or a set member.
    """

    @property
    def package(self) -> str:
        """The package, such as `com.example.app`."""
    @property
    def type(self) -> str:
        """The resource type, such as `string` or `drawable`."""
    @property
    def entry(self) -> str:
        """The entry name, such as `app_name`."""
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...

class ResourceTable:
    """The decoded `resources.arsc` table of an APK.

    It maps resource ids to names and to string values. When a resource has several
    variants, for example one per language, the value of the default configuration is
    used.
    """

    def name_of(self, id: int) -> Optional[ResourceName]:
        """The name of the resource with this id, or `None` when the table has no such
        id.
        """
    def resolve_string(self, id: int) -> Optional[str]:
        """The value of a string resource, or `None` when the id is missing or its value
        is not a string.
        """
    def id_of(self, type: str, entry: str) -> Optional[int]:
        """The id of the resource with this type and entry name, such as `("string",
        "app_name")`, or `None`. It scans the table, so it is meant for single
        lookups, not for loops.
        """
    @property
    def empty(self) -> bool:
        """True when the table has no entries."""

class Certificate:
    """An X.509 certificate that signed an APK."""

    @property
    def der(self) -> bytes:
        """The certificate in its binary (DER) form."""
    @property
    def subject(self) -> str:
        """Who the certificate is for, such as `C=US, O=Example, CN=App`."""
    @property
    def issuer(self) -> str:
        """Who issued the certificate, in the same form. Equal to `subject` for a
        self-signed one.
        """
    @property
    def serial_hex(self) -> str:
        """The serial number, in lowercase hex."""
    @property
    def not_before(self) -> str:
        """Start of the validity period, as `YYYY-MM-DDTHH:MM:SSZ`."""
    @property
    def not_after(self) -> str:
        """End of the validity period, as `YYYY-MM-DDTHH:MM:SSZ`."""
    @property
    def sha1_hex(self) -> str:
        """SHA-1 fingerprint of the certificate, in lowercase hex."""
    @property
    def sha256_hex(self) -> str:
        """SHA-256 fingerprint of the certificate, in lowercase hex. This is the usual
        way to identify a signing certificate.
        """
    @property
    def self_signed(self) -> bool:
        """True when the subject and the issuer are the same."""

class SigningInfo:
    """Which signature schemes an APK carries, and the certificates in them.

    This is identification only. The signatures and digests are **not** verified.
    """

    @property
    def v1(self) -> bool:
        """True when the APK has a JAR signature: a `META-INF/*.RSA`, `*.DSA` or `*.EC`
        file.
        """
    @property
    def v2(self) -> bool:
        """True when the APK has an APK Signature Scheme v2 block."""
    @property
    def v3(self) -> bool:
        """True when the APK has an APK Signature Scheme v3 or v3.1 block."""
    @property
    def certificates(self) -> list[Certificate]:
        """The signing certificates of all schemes. A certificate that appears in
        several schemes is listed once. Empty for an unsigned APK.
        """
    @property
    def is_signed(self) -> bool:
        """True when at least one scheme is present."""

class Apk:
    """An Android package: the archive, its manifest, its resources and its signatures.

    The code is a separate object. `analysis()` returns it. The manifest, the
    resources and the signing information are each decoded on the first read and
    kept.
    """

    @staticmethod
    def open(path: str) -> Apk:
        """Open an APK file.

        Raises:
            FileNotFoundError: The path does not exist.
            OSError: The path exists but cannot be read, for example a directory.
            InvalidApkError: The file is not a ZIP archive.
        """
    def entries(self) -> list[str]:
        """The name of every entry in the archive, in the order of the archive's table
        of contents (the ZIP central directory).

        Each call reads the whole table of contents again and makes a new list, so
        keep the result when you need it more than once.
        """
    def read(self, name: str) -> Optional[bytes]:
        """The uncompressed bytes of one archive entry, or `None` when there is no entry
        with this name.
        """
    @property
    def manifest(self) -> Optional[Manifest]:
        """The decoded manifest, or `None` when the APK has none or it cannot be
        decoded.
        """
    @property
    def resources(self) -> Optional[ResourceTable]:
        """The decoded resource table, or `None` when the APK has none or it cannot be
        decoded.
        """
    def resolve_string(self, reference_id: int) -> Optional[str]:
        """The value of a string resource, through `resources`. `None` when there is no
        resource table or the id is not a string resource.
        """
    @property
    def signing(self) -> SigningInfo:
        """The signature schemes and certificates. An unsigned APK gives an object with
        `is_signed` false.
        """
    def analysis(self) -> AnalysisContext:
        """The code of the APK: an `AnalysisContext` over every `classes*.dex` entry. It
        is built on the first call and kept.

        Raises:
            InvalidApkError: The APK has no `classes.dex`.
            InvalidDexError: One of its DEX files is not valid.
        """

# ===== editable DEX model (dex::model) =====

class Problem:
    """One rule of the DEX format that a model breaks."""

    @property
    def where(self) -> str:
        """Where the problem is, such as `Lcom/a/B;->run()V, item 12`. Empty for a
        problem with the whole file.
        """
    @property
    def message(self) -> str:
        """What is wrong."""

class PoolStats:
    """How many entries each table of the written DEX file would have.

    A DEX file can hold at most 65,535 types and prototypes, and at most 65,536
    fields, methods, method handles and call sites. Use these numbers to decide when
    a file is full.
    """

    @property
    def strings(self) -> int:
        """Number of strings."""
    @property
    def types(self) -> int:
        """Number of types. At most 65,535."""
    @property
    def protos(self) -> int:
        """Number of method prototypes. At most 65,535."""
    @property
    def fields(self) -> int:
        """Number of field identities. At most 65,536."""
    @property
    def methods(self) -> int:
        """Number of method identities. At most 65,536."""
    @property
    def method_handles(self) -> int:
        """Number of method handles."""
    @property
    def call_sites(self) -> int:
        """Number of call sites."""

class ClassInfo:
    """A summary of one class in a `Model`."""

    @property
    def descriptor(self) -> str:
        """The class descriptor."""
    @property
    def access_flags(self) -> AccessFlags:
        """The class's access flags."""
    @property
    def superclass(self) -> Optional[str]:
        """Descriptor of the superclass, or `None` for `java.lang.Object`."""
    @property
    def source_file(self) -> Optional[str]:
        """The source file name recorded for the class, or `None`."""
    @property
    def interfaces(self) -> list[str]:
        """Descriptors of the interfaces the class implements directly, in declaration
        order. For an interface, these are the interfaces it extends.
        """
    @property
    def field_count(self) -> int:
        """Number of fields the class declares."""
    @property
    def method_count(self) -> int:
        """Number of methods the class defines."""

class Model:
    """An editable DEX file in memory.

    Read a DEX file into a model, change it, and write a DEX file again. From Python
    the model works on whole classes: inspect, copy, move, remove and rename.
    Changing the code inside a method is only possible from C++.

    A model is separate from the analysis API. It is not an `AnalysisContext`.

    `len(model)` is the number of classes, and `descriptor in model` tests for a
    class.
    """

    def __init__(self, version: int = ...) -> None:
        """Make an empty model.

        Args:
            version: The DEX format version: 35, 37, 38, 39 or 40.
        """
    @staticmethod
    def read(data: ReadableBuffer, debug_info: bool = ...) -> Model:
        """Read DEX bytes into a model.

        Args:
            data: The content of a DEX file: `bytes`, `bytearray`, `memoryview`,
                or any other object that gives its bytes through the buffer
                protocol.
            debug_info: Keep line numbers and local variable names. False leaves them
                out, which makes the model smaller.

        Raises:
            InvalidDexError: The data is not a valid DEX file.
            UnsupportedDexError: The data uses something the model does not support,
                such as the version 41 container format.
        """
    @staticmethod
    def read_file(path: str, debug_info: bool = ...) -> Model:
        """Read a DEX file into a model.

        Args:
            path: The DEX file.
            debug_info: Keep line numbers and local variable names. False leaves them
                out, which makes the model smaller.

        Raises:
            FileNotFoundError: The file does not exist.
            OSError: The file exists but cannot be read.
            InvalidDexError: The file is not a valid DEX file.
            UnsupportedDexError: The file uses something the model does not support,
                such as the version 41 container format.
        """
    def clone(self) -> Model:
        """A full copy of the model. Changing one does not change the other."""
    version: int
    """The DEX format version `write` uses. `write` never raises it by itself; set it
    when `required_version()` is higher.
    """
    def required_version(self) -> int:
        """The lowest DEX version that has every feature the model uses.

        It runs the same checks as `validate`, so each call costs about as much.
        """
    def pool_stats(self) -> PoolStats:
        """How many entries each table of the written file would have.

        It walks every class and every instruction of the model to collect the
        strings, types, prototypes, fields and methods they use, so each call takes
        time in proportion to the size of the model.
        """
    def validate(self) -> list[Problem]:
        """Every rule of the DEX format the model breaks. An empty list means `write`
        can encode the model. Otherwise `write` raises `InvalidModelError` with the
        same list.
        """
    def __len__(self) -> int: ...
    def class_descriptors(self) -> list[str]:
        """The descriptor of every class, in model order.

        Each call copies every descriptor into a new list. The list does not change
        when the model changes, so it is safe to loop over it while you move or
        remove classes.
        """
    def __contains__(self, descriptor: str) -> bool: ...
    def class_info(self, descriptor: str) -> Optional[ClassInfo]:
        """A summary of the class with this descriptor, or `None` when the model has no
        such class.
        """
    def move_class(self, src: Model, descriptor: str) -> None:
        """Take a class out of another model and put it into this one. The class is not
        copied.

        Raises:
            ModelError: `src` has no such class, or this model already has one.
        """
    def copy_class(self, src: Model, descriptor: str) -> None:
        """Copy a class from another model into this one. The other model is not
        changed.

        Raises:
            ModelError: `src` has no such class, or this model already has one.
        """
    def remove_class(self, descriptor: str) -> bool:
        """Remove the class with this descriptor. Returns whether a class was removed.
        """
    def rename_types(self, mapping: dict[str, str]) -> None:
        """Rename classes everywhere they are referenced.

        The mapping goes from old descriptor to new descriptor. Every reference to a
        class in it is changed: the class itself, superclasses and interfaces, method
        and field identities, array types, instructions, catch types, annotations,
        and the class names inside generic signatures. A class that is not in the
        mapping is left as it is.

        String constants are never changed, so a name passed to `Class.forName` keeps
        its old value.
        """
    def write(self, debug_info: bool = ...) -> bytes:
        """The model as the bytes of a DEX file.

        The model is checked first. The same model always gives the same bytes.

        Args:
            debug_info: Write line numbers and local variable names. False strips them.

        Raises:
            InvalidModelError: The model breaks rules of the format. Its `problems`
                lists them.
            PoolOverflowError: A table has more entries than the format allows.
            VersionTooLowError: A feature needs a higher `version`.
        """
    def write_file(self, path: str, debug_info: bool = ...) -> None:
        """Write the model to a DEX file.

        Args:
            path: The file to write.
            debug_info: Write line numbers and local variable names. False strips them.

        Raises:
            OSError: The file cannot be created or written.
            InvalidModelError: The model breaks rules of the format. Its `problems`
                lists them.
            PoolOverflowError: A table has more entries than the format allows.
            VersionTooLowError: A feature needs a higher `version`.
        """
