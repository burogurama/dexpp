"""Quick start: open an APK, read its metadata, then look at its code."""

import dexpp

apk: dexpp.Apk = dexpp.Apk.open("tests/data/signed.apk")

# Package metadata comes from the manifest. It is None when the APK has none.
manifest: dexpp.Manifest | None = apk.manifest
assert manifest is not None
print("package:", manifest.package, manifest.version_name)
print("permissions:", manifest.permissions)
print("launcher:", manifest.launcher_activity)

# Who signed it.
signing: dexpp.SigningInfo = apk.signing
for certificate in signing.certificates:
    print("signed by:", certificate.subject, certificate.sha256_hex[:16] + "...")

# The code: every classes*.dex of the APK in one analysis context.
ctx: dexpp.AnalysisContext = apk.analysis()
classes: list[dexpp.Class] = ctx.classes()
for cls in classes:
    method_names: list[str] = [method.name for method in cls.methods]
    print(cls.name, method_names)
