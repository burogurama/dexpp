"""APK metadata: manifest, components, resources and signing."""

import dexpp

# --8<-- [start:manifest]
apk: dexpp.Apk = dexpp.Apk.open("tests/data/sample.apk")
manifest: dexpp.Manifest | None = apk.manifest
assert manifest is not None

print(manifest.package, manifest.version_code, manifest.version_name)
print("sdk:", manifest.min_sdk, "to", manifest.target_sdk)
print("uses:", manifest.permissions)
print("declares:", manifest.declared_permissions)
print("debuggable:", manifest.debuggable, "allow_backup:", manifest.allow_backup)
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

for component in guarded.components:
    if component.is_exported is True and component.is_reachable is False:
        permission: str | None = component.permission
        assert permission is not None
        level: int | None = guarded.declared_permission_protection_level(permission)
        print(f"{component.name} is exported, but guarded by {permission} (level {level})")

reachable: list[dexpp.Component] = [c for c in guarded.components if c.is_reachable is True]
print(len(reachable), "of", len(guarded.components), "components are reachable")
# --8<-- [end:reachable]

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
