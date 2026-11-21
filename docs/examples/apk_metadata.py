"""APK metadata: manifest, components, resources and signing."""

import dexpp

# --8<-- [start:manifest]
apk: dexpp.Apk = dexpp.Apk.open("tests/data/sample.apk")
manifest: dexpp.Manifest | None = apk.manifest
assert manifest is not None

print(manifest.package)
print("version:", manifest.version_code, manifest.version_name)
print("sdk:", manifest.min_sdk, "to", manifest.target_sdk)
print("uses:", manifest.permissions)
print("declares:", manifest.declared_permissions)
print("debuggable:", manifest.debuggable, "allow backup:", manifest.allow_backup)
# --8<-- [end:manifest]

# --8<-- [start:components]
for component in manifest.components:
    state: str = "exported" if component.is_exported is True else "private"
    print(component.kind.name, component.name, state)

activities: list[dexpp.Component] = manifest.components_of(dexpp.ComponentKind.ACTIVITY)
launcher: dexpp.Component = activities[0]
print(launcher.intent_filters[0].actions)
# --8<-- [end:components]

# --8<-- [start:reachable]
guarded_apk: dexpp.Apk = dexpp.Apk.open("tests/data/reachable.apk")
guarded: dexpp.Manifest | None = guarded_apk.manifest
assert guarded is not None
print("<application> requires", guarded.application_permission)

for component in guarded.components:
    if component.is_exported is True and component.is_reachable is False:
        permission: str | None = component.permission
        assert permission is not None
        level: int | None = guarded.declared_permission_protection_level(permission)
        source: str = "the application's" if component.permission_inherited else "its own"
        guarded_name: str = component.name.rsplit(".", 1)[1]
        print(f"{guarded_name} is guarded by {source} {permission} (level {level})")

reachable: list[dexpp.Component] = [c for c in guarded.components if c.is_reachable is True]
print(len(reachable), "of", len(guarded.components), "components are reachable")
# --8<-- [end:reachable]

# --8<-- [start:providers]
for provider in guarded.components_of(dexpp.ComponentKind.PROVIDER):
    provider_name: str = provider.name.rsplit(".", 1)[1]
    print(f"{provider_name}: read {provider.read_permission}, write {provider.write_permission}")
    print("  reachable:", provider.is_reachable)
# --8<-- [end:providers]

# --8<-- [start:path_permissions]
for provider in guarded.components_of(dexpp.ComponentKind.PROVIDER):
    path_provider_name: str = provider.name.rsplit(".", 1)[1]
    for rule in provider.path_permissions:
        print(f"{path_provider_name}: {rule.match.name} {rule.path}")
        print(f"  read {rule.read_permission}, write {rule.write_permission}")
    if provider.path_permissions:
        print("  reachable:", provider.is_reachable)
# --8<-- [end:path_permissions]

# --8<-- [start:provider_default]
print("targets API", guarded.target_sdk)
default_provider: dexpp.Component = next(
    c for c in guarded.components if c.name == "com.dexpp.reach.DefaultProvider"
)
print("intent filters:", len(default_provider.intent_filters))
print("exported:", default_provider.exported, "default:", default_provider.exported_default)
print("is_exported:", default_provider.is_exported)
# --8<-- [end:provider_default]

# --8<-- [start:aliases]
for activity in guarded.components_of(dexpp.ComponentKind.ACTIVITY):
    if activity.target_activity is None:
        continue
    alias_name: str = activity.name.rsplit(".", 1)[1]
    target_name: str = activity.target_activity.rsplit(".", 1)[1]
    print(f"{alias_name} starts {target_name}")
    print(f"  permission {activity.permission}, reachable {activity.is_reachable}")
# --8<-- [end:aliases]

# --8<-- [start:resources]
table: dexpp.ResourceTable | None = apk.resources
assert table is not None

app_name: int | None = table.id_of("string", "app_name")
assert app_name is not None
name: dexpp.ResourceName | None = table.name_of(app_name)
assert name is not None

print(hex(app_name), name.package, name.type, name.entry)
print(table.resolve_string(app_name))
print(apk.resolve_string(app_name))
# --8<-- [end:resources]

# --8<-- [start:variants]
refs_apk: dexpp.Apk = dexpp.Apk.open("tests/data/refs.apk")
refs: dexpp.Manifest | None = refs_apk.manifest
refs_table: dexpp.ResourceTable | None = refs_apk.resources
assert refs is not None and refs_table is not None

variant: dexpp.Component = next(
    c for c in refs.components if c.name == "com.dexpp.refs.VariantActivity"
)
print("exported:", variant.exported, "varies:", variant.exported_varies)
print("is_exported:", variant.is_exported)

# The values behind the reference, one per configuration.
reference: int | None = variant.exported_reference
assert reference is not None
for value in refs_table.values(reference):
    print(value.configuration or "default", value.as_bool())

print("debuggable:", refs.debuggable, "varies:", refs.debuggable_varies)
# --8<-- [end:variants]

# --8<-- [start:text_references]
for component in refs.components:
    if component.permission_reference is not None:
        short_name: str = component.name.rsplit(".", 1)[1]
        permission_name: str = component.permission or "none"
        print(f"{short_name}: {permission_name}, reachable: {component.is_reachable}")
# --8<-- [end:text_references]

# --8<-- [start:null_values]
for null_name in ["NullActivity", "NullFilteredActivity", "ThemeActivity"]:
    shown: dexpp.Component = next(c for c in refs.components if c.name.endswith("." + null_name))
    has_reference: bool = shown.exported_reference is not None
    print(f"{null_name}: exported {shown.exported}, reference {has_reference},", end=" ")
    print(f"unknown {shown.exported_unknown}, is_exported {shown.is_exported}")
# --8<-- [end:null_values]

# --8<-- [start:signing]
signed_apk: dexpp.Apk = dexpp.Apk.open("tests/data/signed.apk")
signing: dexpp.SigningInfo = signed_apk.signing

print("signed:", signing.is_signed)
print("v1:", signing.v1, "v2:", signing.v2, "v3:", signing.v3)
for certificate in signing.certificates:
    print(certificate.subject)
    print("valid", certificate.not_before, "to", certificate.not_after)
    print("sha256", certificate.sha256_hex)
# --8<-- [end:signing]

# --8<-- [start:entries]
entries: list[str] = apk.entries()
print(entries)

classes_dex: bytes | None = apk.read("classes.dex")
assert classes_dex is not None
print(len(classes_dex), "bytes of classes.dex")
# --8<-- [end:entries]

# --8<-- [start:memory]
# The bytes can come from anywhere: a download, an upload, a database.
with open("tests/data/sample.apk", "rb") as file:
    data: bytes = file.read()

in_memory: dexpp.Apk = dexpp.Apk.from_bytes(data, label="sample.apk")
in_memory_manifest: dexpp.Manifest | None = in_memory.manifest
assert in_memory_manifest is not None
print(in_memory_manifest.package)

# threads=1 keeps all the work on the calling thread.
code: dexpp.AnalysisContext = in_memory.analysis(threads=1)
print("classes:", [cls.descriptor for cls in code.classes()])

# The label starts the error messages, where Apk.open puts the path.
try:
    dexpp.Apk.from_bytes(b"not an apk", label="upload.apk")
except dexpp.InvalidApkError as error:
    print("InvalidApkError:", error)
# --8<-- [end:memory]
