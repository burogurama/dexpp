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
  `android:exported` attribute is missing.
- `is_exported` is the effective answer. When the attribute is missing, a
  component with at least one intent filter counts as exported. This is the
  rule Android applied before Android 12.

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
3 signatureOrSystem.

## Resources

`apk.resources` is the decoded `resources.arsc` table, or `None` when the APK
has none. It resolves resource ids to names and to string values.

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

Only string values are resolved. When a resource has several variants, for
example one per language, the value of the default configuration is returned.

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

`apk.read(name)` returns the uncompressed bytes of one entry, or `None` when
the archive has no entry with that name.

## Limits

- ZIP64 archives and encrypted entries are not supported.
- Entries must be stored or deflate-compressed.
- When a name appears more than once in the archive, the first entry is used.
