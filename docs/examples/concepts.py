"""Concepts: handles keep the analysis they came from alive."""

import dexpp


# --8<-- [start:handles]
def main_class(path: str) -> dexpp.Class | None:
    """Load a DEX file and return one of its classes."""
    ctx: dexpp.AnalysisContext = dexpp.AnalysisContext.from_dex(path)
    return ctx.find_class("LTestDex;")  # ctx goes out of scope here


cls: dexpp.Class | None = main_class("tests/data/classes.dex")
assert cls is not None
print(cls.descriptor, [method.name for method in cls.methods])  # still works
# --8<-- [end:handles]
