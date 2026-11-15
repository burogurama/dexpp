"""Editing DEX files with dexpp.Model: inspect, split, rename, write."""

import os
import tempfile

import dexpp

# --8<-- [start:read]
model: dexpp.Model = dexpp.Model.read_file("tests/data/shapes.dex")
print("DEX version", model.version, "with", len(model), "classes")
print(model.class_descriptors())

info: dexpp.ClassInfo | None = model.class_info("LCircle;")
assert info is not None
print(info.descriptor, "extends", info.superclass, "-", info.method_count, "methods")
print(model.class_interfaces("LAbstractShape;"))
# --8<-- [end:read]

# --8<-- [start:split]
out_dir: str = tempfile.mkdtemp()
for index, descriptor in enumerate(model.class_descriptors()):
    one: dexpp.Model = dexpp.Model(model.version)
    one.copy_class(model, descriptor)
    one.write_file(os.path.join(out_dir, f"class{index}.dex"))

file_names: list[str] = sorted(os.listdir(out_dir))
print(file_names)
# --8<-- [end:split]

# --8<-- [start:merge]
merged: dexpp.Model = dexpp.Model(model.version)
for file_name in file_names:
    part: dexpp.Model = dexpp.Model.read_file(os.path.join(out_dir, file_name))
    for descriptor in part.class_descriptors():
        merged.move_class(part, descriptor)

print(len(merged), "classes")
print("same bytes as the original model:", merged.write() == model.write())
# --8<-- [end:merge]

# --8<-- [start:rename]
renames: dict[str, str] = {"LCircle;": "Lcom/example/Round;"}
model.rename_types(renames)
print(model.class_descriptors())
# --8<-- [end:rename]

# --8<-- [start:validate]
problems: list[dexpp.Problem] = model.validate()
print("problems:", problems)

stats: dexpp.PoolStats = model.pool_stats()
print(stats.strings, "strings,", stats.types, "types,", stats.methods, "methods")
needed: int = model.required_version()
print("needs DEX version", needed)
# --8<-- [end:validate]

# --8<-- [start:write]
data: bytes = model.write()
again: dexpp.Model = dexpp.Model.read(data)
print(len(data), "bytes;", "writing is repeatable:", again.write() == data)
# --8<-- [end:write]

# --8<-- [start:errors]
try:
    dexpp.Model.read(b"not a dex file")
except dexpp.InvalidDexError as error:
    print("InvalidDexError:", error)
# --8<-- [end:errors]

# --8<-- [start:invalid]
broken: dexpp.Model = dexpp.Model.read_file("tests/data/shapes.dex")
broken.rename_types({"LCircle;": "not a descriptor"})
try:
    broken.write()
except dexpp.InvalidModelError as error:
    print(len(error.problems), "problems")
    for problem in error.problems:
        print(" ", problem.where, "-", problem.message)
# --8<-- [end:invalid]

# --8<-- [start:version]
defaults: dexpp.Model = dexpp.Model.read_file("tests/data/inherit.dex")
defaults.version = 35
try:
    defaults.write()
except dexpp.VersionTooLowError as error:
    print("VersionTooLowError:", error)

defaults.version = defaults.required_version()
print("written with version", defaults.version, "-", len(defaults.write()), "bytes")
# --8<-- [end:version]
