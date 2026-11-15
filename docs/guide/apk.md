# APK metadata

An `Apk` is the *package*: the archive, its manifest, its resources and its
signatures. The code inside it is a separate object, the `AnalysisContext`,
which you get with `apk.analysis()`.

```python
apk: dexpp.Apk = dexpp.Apk.open("app.apk")
```

`Apk.open` raises `FileNotFoundError` when the path does not exist, `OSError`
when it exists but cannot be read (a directory, for example), and
`dexpp.InvalidApkError` when the file is not a ZIP archive.

`apk.analysis()` raises `dexpp.InvalidApkError` when the APK has no
`classes.dex`, and `dexpp.InvalidDexError` when one of its DEX files cannot be
parsed.

## The manifest

`apk.manifest` is the decoded `AndroidManifest.xml`. It is `None` when the APK
has no manifest or the manifest cannot be decoded. So its type is
`Manifest | None`, and the example checks it before use.

```python
--8<-- "apk_metadata.py:manifest"
```

```text
--8<-- "output/apk_metadata.manifest.txt"
```

Attributes that the manifest may leave out, such as `version_code`, `min_sdk`
or `debuggable`, are `None` when they are not set. So `debuggable is None`
means "not written in the manifest", which Android treats as false.

### Values given by a resource reference

A manifest can give a boolean or integer attribute through a resource
reference instead of a plain value, for example
`android:debuggable="@bool/debug"` or `android:exported="@bool/is_exported"`.
dexpp then looks the value up in the APK's resource table (`resources.arsc`),
in the default configuration, and the property holds that value. The
reference id itself is kept in a second property: `version_code_reference`,
`min_sdk_reference`, `target_sdk_reference`, `debuggable_reference`,
`allow_backup_reference`, and `exported_reference` on a component. These are
`None` for a plain value or a missing attribute.

A reference can fail to resolve: the APK has no resource table, the table has
no such id, the value has another type (a string where a boolean is expected),
or the attribute names a theme attribute (`?attr/...`), whose value depends on
the theme the app runs with. Then the value property is `None`, but the
`..._reference` property still holds the id. So the two cases stay apart:

- value `None`, reference `None`: the attribute is not in the manifest;
- value `None`, reference set: the attribute is in the manifest, but its value
  is unknown.

When a resource has variants, for example a `values-v31` folder that changes a
boolean on Android 12 and newer, only the default value is used. On a device
that matches the variant, Android uses the other value.

dexpp reads the `android:` attributes the way Android does: by their resource
id, not by their name. In the binary manifest, each attribute name is a string,
and a table in the file gives the framework attributes their ids, such as
`0x01010010` for `android:exported`. Android only looks at the id. So an app
that renames the attribute strings cannot hide a value from dexpp, and an
attribute that is only named like `android:exported`, without its id, is
ignored, as Android ignores it.

## Components

A component is an activity, a service, a broadcast receiver or a content
provider. Component names are always full class names: a manifest entry such
as `.MainActivity` is completed with the package name.

```python
--8<-- "apk_metadata.py:components"
```

```text
--8<-- "output/apk_metadata.components.txt"
```

There are two ways to ask whether a component is exported:

- `exported` is what the manifest says: `True`, `False`, or `None` when the
  `android:exported` attribute is missing or is a reference that could not be
  resolved.
- `is_exported` is the effective answer. When the attribute is missing, a
  component with at least one intent filter counts as exported. This is the
  rule Android applied before Android 12. When the attribute is a reference
  that could not be resolved (`exported is None` and `exported_reference` is
  set), `is_exported` is `True`: the developer set the flag, its value is
  unknown, and for triage "possibly exported" is the safe answer. Guessing
  from the intent filters could hide an exported component.

## Which components can another app reach

Exported is not the same as reachable. An exported component can still be
protected by a permission that no other app can get.

`is_reachable` is true when the component is exported **and** it is not
guarded by a signature-level permission that the app itself declares. A
permission with the `signature` or `signatureOrSystem` protection level is
only granted to apps signed with the same key, so a third-party app cannot
hold it.

```python
--8<-- "apk_metadata.py:reachable"
```

```text
--8<-- "output/apk_metadata.reachable.txt"
```

Two cases stay reachable on purpose:

- A component guarded by a `normal` or `dangerous` permission. Any app can
  request those.
- A component guarded by a permission the manifest does not declare, such as a
  platform permission. dex++ cannot know its protection level from this APK
  alone, so it does not assume the component is protected.

`declared_permission_protection_level(name)` returns the raw Android value for
a permission the app declares, or `None` for a permission it does not declare.
The low four bits are the base level: 0 normal, 1 dangerous, 2 signature,
3 signatureOrSystem. A protection level given by a resource reference is
resolved like the other values. When it cannot be resolved, the function
returns `None` for that declared permission too, and the permission does not
count as a guard: a component it protects stays reachable.

## Resources

`apk.resources` is the decoded `resources.arsc` table, or `None` when the APK
has none or it cannot be decoded. It resolves resource ids to names and to
string, boolean and integer values.

```python
--8<-- "apk_metadata.py:resources"
```

```text
--8<-- "output/apk_metadata.resources.txt"
```

- `id_of(type, entry)` finds the id of a resource by name.
- `name_of(id)` gives the package, type and entry name of an id.
- `resolve_string(id)` gives the value of a string resource. It returns `None`
  when the id is missing or its value is not a string.
- `apk.resolve_string(id)` is a shortcut for the same lookup.
- `resolve_bool(id)` and `resolve_int(id)` give the value of a boolean or an
  integer resource. Like Android, both accept any integer-typed value: a
  boolean read as an integer gives its raw data (aapt2 writes `true` as -1),
  and an integer read as a boolean is true when it is not 0. They return
  `None` when the id is missing or its value has another type, such as a
  string.

A resource whose value is a reference to another resource, such as
`<bool name="a">@bool/b</bool>`, is followed to the end, for up to 20 steps as
Android does. When a resource has several variants, for example one per
language, the value of the default configuration is returned.

## Signing

`apk.signing` tells you which signature schemes are present and which
certificates signed the APK. It never fails: an unsigned APK gives an object
with `is_signed == False` and no certificates.

```python
--8<-- "apk_metadata.py:signing"
```

```text
--8<-- "output/apk_metadata.signing.txt"
```

`v1` is JAR signing (the `META-INF/*.RSA`, `*.DSA` or `*.EC` files). `v2` and
`v3` are the APK Signature Schemes; `v3` also covers v3.1. A certificate that
appears in several schemes is listed once.

!!! warning "Identification only"
    dex++ reads the certificates. It does **not** check that the signatures
    are valid or that the file's content matches its digests. Use Android's
    `apksigner verify` when you need to know that an APK is correctly signed.

## Archive entries

```python
--8<-- "apk_metadata.py:entries"
```

```text
--8<-- "output/apk_metadata.entries.txt"
```

`apk.entries()` returns the name of every entry, in the order of the
archive's table of contents (the ZIP central directory). Each call reads the
whole table of contents again, and a large app can have thousands of entries,
so keep the list when you need it more than once.

`apk.read(name)` returns the uncompressed bytes of one entry, or `None` when
the archive has no entry with that name.

## Limits

- ZIP64 archives and encrypted entries are not supported.
- Entries must be stored or deflate-compressed.
- When a name appears more than once in the archive, the first entry is used.
