"""Errors: what dexpp raises for a file it cannot use, and how to catch it."""

import dexpp


# --8<-- [start:load]
def package_of(path: str) -> str | None:
    """The package name of an APK, or None when the file cannot be used."""
    try:
        apk: dexpp.Apk = dexpp.Apk.open(path)
    except FileNotFoundError:
        print(path, "does not exist")
        return None
    except dexpp.InvalidApkError as error:
        print("not an APK:", error)
        return None

    manifest: dexpp.Manifest | None = apk.manifest
    if manifest is None:
        return None
    return manifest.package


print(package_of("tests/data/sample.apk"))
print(package_of("tests/data/missing.apk"))
print(package_of("tests/data/classes.dex"))
# --8<-- [end:load]

# --8<-- [start:base]
try:
    dexpp.AnalysisContext.from_dex("tests/data/TestDex.java")
except dexpp.Error as error:
    print(type(error).__name__, "-", error)
# --8<-- [end:base]
