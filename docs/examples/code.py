"""Classes, methods, bytecode, control flow, try blocks and annotations."""

import dexpp

# --8<-- [start:load]
apk_ctx: dexpp.AnalysisContext = dexpp.AnalysisContext.from_apk("tests/data/app.apk")
dex_ctx: dexpp.AnalysisContext = dexpp.AnalysisContext.from_dex("tests/data/classes.dex")
split_ctx: dexpp.AnalysisContext = dexpp.AnalysisContext.from_dex_files(
    ["tests/data/inherit_a.dex", "tests/data/inherit_b.dex"]
)
print(len(apk_ctx.classes()), len(dex_ctx.classes()), len(split_ctx.classes()))
# --8<-- [end:load]

# --8<-- [start:classes]
ctx: dexpp.AnalysisContext = dexpp.AnalysisContext.from_dex("tests/data/shapes.dex")

classes: list[dexpp.Class] = ctx.classes()
for cls in classes:
    kind: str = "interface" if cls.is_interface is True else "class"
    superclass: dexpp.ClassRef | None = cls.superclass
    parent: str | None = superclass.descriptor if superclass is not None else None
    interfaces: list[str] = [interface.descriptor for interface in cls.interfaces]
    print(kind, cls.descriptor, "extends", parent, "implements", interfaces)

circle: dexpp.Class | None = ctx.find_class("LCircle;")
assert circle is not None
print([field.name + ":" + field.type for field in circle.fields])

missing: dexpp.Class | None = ctx.find_class("Lno/such/Class;")
print(missing)
# --8<-- [end:classes]

# --8<-- [start:methods]
for method in circle.methods:
    visibility: str = "public" if method.is_public is True else "non-public"
    print(visibility, method.name, method.parameters, "->", method.return_type)

constructor: dexpp.Method = circle.methods[0]
flags: dexpp.AccessFlags = constructor.access_flags
print(constructor.name, int(flags), dexpp.AccessFlags.CONSTRUCTOR in flags)
# --8<-- [end:methods]

# --8<-- [start:instructions]
area: dexpp.Method = next(method for method in circle.methods if method.name == "area")
instructions: dexpp.InstructionView = area.instructions

for instruction in instructions:
    print(f"{instruction.base.offset:04x}  {dexpp.to_string(instruction)}")
# --8<-- [end:instructions]

# --8<-- [start:disassemble]
test_ctx: dexpp.AnalysisContext = dexpp.AnalysisContext.from_dex("tests/data/classes.dex")
test_class: dexpp.Class | None = test_ctx.find_class("LTestDex;")
assert test_class is not None

for method in test_class.methods:
    if method.name in ("helloDex", "main"):
        print(method.name + method.proto)
        lines: list[dexpp.DisassemblyLine] = method.disassemble()
        for line in lines:
            print(f"{line.offset:04x}  {line.text}")
# --8<-- [end:disassemble]

# --8<-- [start:typed]
for instruction in instructions:
    if isinstance(instruction, dexpp.FieldInstruction):
        action: str = "write" if instruction.is_write is True else "read"
        print("field access at", instruction.base.offset, action)

calls: list[dexpp.MethodCall] = area.calls()
accesses: list[dexpp.FieldAccess] = area.field_accesses()
print([(call.target.class_descriptor, call.target.name) for call in calls])
print([(access.field.name, access.is_write) for access in accesses])
# --8<-- [end:typed]

# --8<-- [start:registers]
print(area.register_count, "registers")

parameters: list[dexpp.ParameterRegister] = area.parameter_registers
for parameter in parameters:
    note: str = "(this)" if parameter.is_this is True else ""
    print(f"v{parameter.reg}", parameter.type, note)
# --8<-- [end:registers]

# --8<-- [start:cfg]
fibonacci_ctx: dexpp.AnalysisContext = dexpp.AnalysisContext.from_dex("tests/data/fibonacci.dex")
fibonacci: dexpp.Class | None = fibonacci_ctx.find_class("LFibonacci;")
assert fibonacci is not None
compute: dexpp.Method = next(m for m in fibonacci.methods if m.name == "compute")

cfg: dexpp.Cfg = compute.cfg
for block in cfg.blocks:
    edges: list[tuple[str, int]] = [(e.kind.name, e.target_block) for e in block.successors]
    print(f"block {block.index} [{block.start_offset:#x}, {block.end_offset:#x})", edges)
# --8<-- [end:cfg]

# --8<-- [start:handlers]
parse_ctx: dexpp.AnalysisContext = dexpp.AnalysisContext.from_dex("tests/data/trycatch.dex")
parse_class: dexpp.Class | None = parse_ctx.find_class("LTryCatch;")
assert parse_class is not None
safe_parse: dexpp.Method = next(m for m in parse_class.methods if m.name == "safeParse")

for block in safe_parse.cfg.blocks:
    for edge in block.successors:
        if edge.kind is dexpp.CfgEdgeKind.EXCEPTION:
            print(f"block {block.index} -> {edge.target_block}: catches {edge.catch_type}")
        elif edge.kind is dexpp.CfgEdgeKind.EXCEPTION_CATCH_ALL:
            print(f"block {block.index} -> {edge.target_block}: catches everything")
# --8<-- [end:handlers]

# --8<-- [start:try]
try_catch_ctx: dexpp.AnalysisContext = dexpp.AnalysisContext.from_dex("tests/data/trycatch.dex")
try_catch: dexpp.Class | None = try_catch_ctx.find_class("LTryCatch;")
assert try_catch is not None
divide: dexpp.Method = next(m for m in try_catch.methods if m.name == "divide")

try_blocks: list[dexpp.TryBlock] = divide.try_blocks
for try_block in try_blocks:
    print(f"try [{try_block.start_offset:#x}, {try_block.end_offset:#x})")
    for handler in try_block.handlers:
        print("  catch", handler.type_descriptor, "at", hex(handler.handler_offset))
    print("  catch-all at", try_block.catch_all_offset)
# --8<-- [end:try]

# --8<-- [start:annotations]
annotated_ctx: dexpp.AnalysisContext = dexpp.AnalysisContext.from_dex("tests/data/annotated.dex")
annotated: dexpp.Class | None = annotated_ctx.find_class("LAnnotated;")
assert annotated is not None

annotations: list[dexpp.Annotation] = annotated.annotations
for annotation in annotations:
    print(annotation.type_descriptor, annotation.visibility.name, annotation.elements)

fields: dict[str, dexpp.Field] = {field.name: field for field in annotated.fields}
magic: dexpp.AnnotationValue = fields["MAGIC"].initial_value
greeting: dexpp.AnnotationValue = fields["GREETING"].initial_value
print(magic, repr(greeting))
# --8<-- [end:annotations]

# --8<-- [start:strings]
hello_ctx: dexpp.AnalysisContext = dexpp.AnalysisContext.from_dex("tests/data/classes.dex")
strings: list[str] = hello_ctx.strings()
print(len(strings), "strings;", "Hello Dex++!!" in strings)
# --8<-- [end:strings]
