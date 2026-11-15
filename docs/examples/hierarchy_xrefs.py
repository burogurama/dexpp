"""The class hierarchy and the cross-reference index."""

import re

import dexpp

# --8<-- [start:hierarchy]
shapes: dexpp.AnalysisContext = dexpp.AnalysisContext.from_dex("tests/data/shapes.dex")
hierarchy: dexpp.ClassHierarchy = shapes.class_hierarchy

supertypes: list[str] = hierarchy.supertypes("LCircle;")
ancestors: list[str] = hierarchy.all_supertypes("LCircle;")
descendants: list[str] = hierarchy.all_descendants("LShape;")
print(supertypes)
print(ancestors)
print(sorted(descendants))
print(hierarchy.implementers("LShape;"))
print(sorted(hierarchy.subclasses("LAbstractShape;")))

is_shape: bool = hierarchy.is_subtype_of("LCircle;", "LShape;")
print(is_shape, hierarchy.is_interface("LShape;"))
print(hierarchy.is_loaded("Ljava/lang/Object;"))
# --8<-- [end:hierarchy]

# --8<-- [start:method_refs]
ctx: dexpp.AnalysisContext = dexpp.AnalysisContext.from_dex("tests/data/classes.dex")
xrefs: dexpp.Xrefs = ctx.xrefs

matches: list[tuple[dexpp.MethodId, list[dexpp.MethodXref]]] = xrefs.method_refs_by_name("println")
for target, sites in matches:
    for site in sites:
        caller: dexpp.MethodId = xrefs.referrers[site.referrer]
        called: str = f"{target.class_descriptor} {target.name}"
        print(f"{called} is called by {caller.name} at {site.code_offset}")
# --8<-- [end:method_refs]

# --8<-- [start:strings]
loaded: list[str] = xrefs.referenced_strings()
print(loaded)

string_sites: list[dexpp.StringXref] = xrefs.string_refs("Hello Dex++!!")
for string_site in string_sites:
    method: dexpp.Method | None = xrefs.referrer_method(string_site.referrer)
    assert method is not None
    print("loaded in", method.declaring_class.descriptor, method.name)
# --8<-- [end:strings]

# --8<-- [start:pattern]
greetings_ctx: dexpp.AnalysisContext = dexpp.AnalysisContext.from_dex("tests/data/multistring.dex")
greetings: dexpp.Xrefs = greetings_ctx.xrefs

world: re.Pattern[str] = re.compile(r"World!$")
values: list[str] = sorted(greetings.referenced_strings())  # in no fixed order
for value in values:
    if world.search(value) is not None:
        for pattern_site in greetings.string_refs(value):
            loader: dexpp.MethodId = greetings.referrers[pattern_site.referrer]
            print(repr(value), "loaded by", loader.name, "at", pattern_site.code_offset)
# --8<-- [end:pattern]

# --8<-- [start:fields]
out: dexpp.FieldId = dexpp.FieldId("Ljava/lang/System;", "out", "Ljava/io/PrintStream;")
reads: list[dexpp.FieldXref] = xrefs.field_reads(out)
writes: list[dexpp.FieldXref] = xrefs.field_writes(out)
print(len(reads), "reads,", len(writes), "writes")
# --8<-- [end:fields]

# --8<-- [start:types]
pets_ctx: dexpp.AnalysisContext = dexpp.AnalysisContext.from_dex("tests/data/polymorphic.dex")
pets: dexpp.Xrefs = pets_ctx.xrefs

type_sites: list[dexpp.TypeXref] = pets.type_refs("Ljava/lang/StringBuilder;")
for type_site in type_sites:
    user: dexpp.MethodId = pets.referrers[type_site.referrer]
    print(type_site.kind.name, "in", user.class_descriptor, user.name, "at", type_site.code_offset)
# --8<-- [end:types]
