# APK metadata

An APK object is the *package*: the archive, its manifest, its resources and
its signatures. The code inside it is a separate object, the analysis context,
which the APK's `analysis()` method returns (Go: `Analysis()`).

The examples come in Python, Go and Java. Pick your language in the tabs.

| Open an APK | Python | Go | Java |
| ----------- | ------ | -- | ---- |
| open the archive | `dexpp.Apk.open(path)` | `dexpp.OpenAPK(path)` | `Apk.open(path)` |
| open bytes in memory | `dexpp.Apk.from_bytes(data)` | `dexpp.OpenAPKBytes(data, label)` | `Apk.fromBytes(data)` |
| get its code | `apk.analysis()` | `apk.Analysis()` | `apk.analysis()` |

Opening fails when the path does not exist, when it exists but cannot be read
(a directory, for example), or when the file is not a ZIP archive. Getting the
code fails when the APK has no `classes.dex`, or when one of its DEX files
cannot be parsed.

| Failure | Python raises | Go returns | Java throws |
| ------- | ------------- | ---------- | ----------- |
| the path does not exist | `FileNotFoundError` | `dexpp.ErrFileNotFound` | `NoSuchFileException` |
| the path cannot be read | `OSError` | `dexpp.ErrCannotRead` | `FileSystemException` |
| not a ZIP archive, or no `classes.dex` | `dexpp.InvalidApkError` | `dexpp.ErrInvalidAPK` | `InvalidApkException` |
| a DEX file cannot be parsed | `dexpp.InvalidDexError` | `dexpp.ErrInvalidDex` | `InvalidDexException` |

In Go, compare an error with these values through `errors.Is`. See
[Errors in Go](../c-go.md#errors-in-go) and [Errors](../java.md#errors) on the
Java page.

## Opening an APK from memory

Sometimes a program has the bytes of an APK, not a file: a server that
receives an upload, or a tool that downloads APKs. It can open the bytes
directly, without writing them to a file first.

<!-- example: apk_metadata.memory -->

- The APK keeps its own copy of the bytes. Your copy can be changed or freed
  as soon as the call returns.
- The bytes get the same checks as a file given to `open`. Bytes that are not
  a ZIP archive fail with the same invalid APK error.
- The label is a name for the bytes, such as the name of an uploaded file.
  Error messages start with it, where `open` puts the path. The errors of the
  APK's `analysis()` use it too. Without a label, the messages start with
  `<apk>`. In Go the label is a required argument, and an empty label gives
  `<apk>`.

### The threads of the analysis

The APK's `analysis()` method takes a thread count, like loading an APK with
the analysis context (see [Threads](concepts.md#threads)):

| Language | Get the code on the calling thread only |
| -------- | --------------------------------------- |
| Python | `apk.analysis(threads=1)` |
| Go | `apk.Analysis(dexpp.WithThreads(1))` |
| Java | `apk.analysis(1)` |

A count of 1 keeps all the work on the calling thread: decompressing the DEX
files now, and building the call graph later. Use it when your program
already analyses several APKs in parallel, or when it must not start threads.
A count of 0, the default, lets the library choose.

The APK keeps the analysis it builds. The first call builds it, and a later
call with the same thread count returns the same analysis, so its call graph
and other structures are built only once. A call with a different thread
count builds a second, separate analysis, which holds its own copy of the DEX
files. Both give the same answers. So choose one thread count and use it
everywhere in a program.

## The manifest

The manifest is the decoded `AndroidManifest.xml`. An APK may have no manifest,
or one that cannot be decoded. Then there is no manifest: Python gives `None`,
Go gives `false` as the second result of `Manifest()`, and Java gives an empty
`Optional`. The examples check this before they use the manifest.

<!-- example: apk_metadata.manifest -->

Some attributes may be left out of a manifest, such as the version code, the
minimum SDK or `debuggable`. When one is not set, its value is absent: `None`
in Python, a `nil` pointer in Go, an empty `Optional` in Java. So an absent
`debuggable` means "not written in the manifest", which Android treats as
false.

### Values given by a resource reference

A manifest can give an attribute through a resource reference instead of a
plain value, for example `android:debuggable="@bool/debug"`,
`android:exported="@bool/is_exported"` or
`android:permission="@string/guard"`. dexpp then looks the value up in the
APK's resource table (`resources.arsc`), and the attribute holds that value.
The reference id itself is kept in a second attribute, whose name ends in
`reference`:

| Value | Python | Go | Java |
| ----- | ------ | -- | ---- |
| version code | `version_code_reference` | `VersionCodeReference` | `versionCodeReference()` |
| version name | `version_name_reference` | `VersionNameReference` | `versionNameReference()` |
| minimum SDK | `min_sdk_reference` | `MinSDKReference` | `minSdkReference()` |
| target SDK | `target_sdk_reference` | `TargetSDKReference` | `targetSdkReference()` |
| `debuggable` | `debuggable_reference` | `DebuggableReference` | `debuggableReference()` |
| `allowBackup` | `allow_backup_reference` | `AllowBackupReference` | `allowBackupReference()` |
| `name`, on a component | `name_reference` | `NameReference` | `nameReference()` |
| `exported`, on a component | `exported_reference` | `ExportedReference` | `exportedReference()` |
| `permission`, on a component | `permission_reference` | `PermissionReference` | `permissionReference()` |

The reference is absent for a plain value or a missing attribute.

A reference can fail to resolve: the APK has no resource table, the table has
no such id, the value has a type the attribute cannot use (a string where a
number is expected), or the attribute names a theme attribute (`?attr/...`),
whose value depends on the theme the app runs with. Then the value is absent,
but the reference still holds the id. So the two cases stay apart:

- value absent, reference absent: the attribute is not in the manifest;
- value absent, reference set: the attribute is in the manifest, but its value
  is unknown.

dexpp reads the `android:` attributes the way Android does: by their resource
id, not by their name. In the binary manifest, each attribute name is a string,
and a table in the file gives the framework attributes their ids, such as
`0x01010010` for `android:exported`. Android only looks at the id. So an app
that renames the attribute strings cannot hide a value from dexpp, and an
attribute that is only named like `android:exported`, without its id, is
ignored, as Android ignores it.

### Values that differ between configurations

A resource can have a different value in each *configuration*: one per
language, screen size, Android version, and so on. In the app's sources these
are the `res/values-...` folders. A device uses the value of the configuration
that matches it. For example, an app can write `android:exported="@bool/open"`
with `open` false in `res/values` and true in `res/values-v31`. Then the
component is exported on Android 12 (API level 31) and newer, and private on
older versions.

dexpp does not know which device the app will run on, so it looks at every
configuration:

- A boolean attribute (`exported`, `debuggable`, `allowBackup`) holds the
  value of the default configuration, `res/values`. A second attribute, whose
  name ends in `varies`, is true when another configuration gives another
  value: `exported_varies` on a component, `debuggable_varies` and
  `allow_backup_varies` on the manifest (Go: `ExportedVaries`,
  `DebuggableVaries`, `AllowBackupVaries`; Java: `exportedVaries()`,
  `debuggableVaries()`, `allowBackupVaries()`).
- `is_exported` and `is_reachable` take the unsafe answer: a component whose
  `exported` varies counts as exported. For `debuggable` and `allowBackup`,
  treat a value that varies as possibly true.
- A protection level given by a reference works the same way:
  `declared_permission_protection_level(name)` gives the default
  configuration's level, and `declared_permission_protection_level_varies(name)`
  (Go: `DeclaredPermissionProtectionLevelVaries`, Java:
  `declaredPermissionProtectionLevelVaries`) says whether it varies. A
  permission whose level is below signature in some configuration does not
  guard a component: on a device that matches that configuration, any app can
  hold it.
- The integers (the version code and the SDK levels) hold the default
  configuration's value. There is no `varies` attribute for them.

The resource table lists the value of every configuration (see
[Resources](#resources)):

<!-- example: apk_metadata.variants -->

### Text given by a reference

Android reads `android:name`, `android:permission` and `android:versionName`
in a way that ignores a string that changes with the configuration. If the
string has a variant for a language, a screen or any other configuration except
the Android version, Android uses no value at all. dexpp does the same: the
value is absent and the reference is set. A string that changes only with the
Android version is read by Android, but its value then depends on the device,
so dexpp leaves the value absent too.

This matters most for `android:permission`. A component guarded by
`@string/guard` is only protected when the string has one value. When it
changes with the language, Android ignores it and the component has no
permission of its own, so dexpp reports no permission and the component stays
reachable.

<!-- example: apk_metadata.text_references -->

Some names are never resolved, because Android does not resolve them either:
the name of a `<uses-permission>` (Android ignores a reference there) and the
names of the `<action>` and `<category>` elements of an intent filter.

### Text where a boolean is expected

A binary manifest made by hand, or changed by an obfuscator, can store
`android:exported` as the text `"true"` instead of a boolean. A reference can
also point to a `<string>` resource. Android reads such text as true only when
it is exactly `"1"`, `"true"` or `"TRUE"`. Any other text, such as `"True"` or
`"yes"`, is false, and empty text gives the attribute's default (false for
`exported` and `debuggable`, true for `allowBackup`). dexpp follows the same
rule, everywhere a boolean is read.

## Components

A component is an activity, a service, a broadcast receiver or a content
provider. Component names are always full class names: a manifest entry such
as `.MainActivity` is completed with the package name.

<!-- example: apk_metadata.components -->

There are two ways to ask whether a component is exported:

- `exported` (Go: `Exported`, Java: `exported()`) is what the manifest says:
  true, false, or absent when the `android:exported` attribute is missing or
  is a reference that could not be resolved. For a reference, it is the value
  of the default configuration.
- `is_exported` (Go: `IsExported`, Java: `isExported()`) is the effective
  answer. When the attribute is missing, a component with at least one intent
  filter counts as exported. This is the rule Android applied before
  Android 12. When the attribute is a reference that could not be resolved
  (the value is absent and the reference is set), `is_exported` is true: the
  developer set the flag, its value is unknown, and for triage "possibly
  exported" is the safe answer. Guessing from the intent filters could hide an
  exported component. When the attribute is a reference whose value varies
  between configurations, `is_exported` is true too.

## Which components can another app reach

Exported is not the same as reachable. An exported component can still be
protected by a permission that no other app can get.

`is_reachable` (Go: `IsReachable`, Java: `isReachable()`) is true when the
component is exported **and** it is not guarded by a signature-level
permission that the app itself declares. A permission with the `signature` or
`signatureOrSystem` protection level is only granted to apps signed with the
same key, so a third-party app cannot hold it.

<!-- example: apk_metadata.reachable -->

Two cases stay reachable on purpose:

- A component guarded by a `normal` or `dangerous` permission. Any app can
  request those.
- A component guarded by a permission the manifest does not declare, such as a
  platform permission. dex++ cannot know its protection level from this APK
  alone, so it does not assume the component is protected.
- A component whose `android:permission` is a reference without a single value
  (see [Text given by a reference](#text-given-by-a-reference)), or whose
  permission has a protection level that is below signature in some
  configuration.

`declared_permission_protection_level(name)` (Go:
`DeclaredPermissionProtectionLevel`, Java:
`declaredPermissionProtectionLevel`) returns the raw Android value for a
permission the app declares. For a permission it does not declare, there is no
value: `None` in Python, `false` as the second result in Go, an empty
`OptionalInt` in Java. The low four bits are the base level: 0 normal,
1 dangerous, 2 signature, 3 signatureOrSystem. A protection level given by a
resource reference is resolved like the other values. When it cannot be
resolved, there is no value for that declared permission either, and the
permission does not count as a guard: a component it protects stays reachable.

## Resources

The resource table is the decoded `resources.arsc`. An APK may have none, or
one that cannot be decoded: then Python gives `None`, Go gives `nil` and Java
gives an empty `Optional`. The table resolves resource ids to names and to
string, boolean and integer values.

<!-- example: apk_metadata.resources -->

| Lookup | Python | Go | Java |
| ------ | ------ | -- | ---- |
| the id of a resource, by type and entry name | `id_of(type, entry)` | `IDOf(type, entry)` | `idOf(type, entry)` |
| the package, type and entry name of an id | `name_of(id)` | `NameOf(id)` | `nameOf(id)` |
| the value of a string resource | `resolve_string(id)` | `ResolveString(id)` | `resolveString(id)` |
| the value of a boolean resource | `resolve_bool(id)` | `ResolveBool(id)` | `resolveBool(id)` |
| the value of an integer resource | `resolve_int(id)` | `ResolveInt(id)` | `resolveInt(id)` |
| the value in every configuration | `values(id)` | `Values(id)` | `values(id)` |

- Each lookup has no result when the id is missing or its value has another
  type, such as a string where an integer is asked for.
- The APK's own `resolve_string(id)` (Go: `ResolveString`, Java:
  `resolveString`) is a shortcut for the same string lookup.
- Like Android, the boolean and the integer lookups accept any integer-typed
  value: a boolean read as an integer gives its raw data (aapt2 writes `true`
  as -1), and an integer read as a boolean is true when it is not 0. The
  boolean lookup also reads text the way Android does (see
  [Text where a boolean is expected](#text-where-a-boolean-is-expected)).

A resource whose value is a reference to another resource, such as
`<bool name="a">@bool/b</bool>`, is followed to the end, for up to 20 steps as
Android does. When a resource has several variants, for example one per
language, the `resolve` lookups return the value of the default configuration.

`values(id)` returns every variant, in the order of the table. Each value has
these parts:

| Part | Python | Go | Java |
| ---- | ------ | -- | ---- |
| the configuration, such as `""` (default), `"v31"` or `"fr"` | `configuration` | `Configuration` | `configuration()` |
| the raw type, such as `0x12` for a boolean or `0x03` for a string | `type` | `Type` | `type()` |
| the raw 32-bit data | `data` | `Data` | `data()` |
| the text of a string value | `string` | `String` | `string()` |
| the value read as a boolean | `as_bool()` | `AsBool()` | `asBool()` |
| the value read as an integer | `as_int()` | `AsInt()` | `asInt()` |

The configuration is written like the name of a `res/values-...` folder,
without `values-`, as `aapt2 dump resources` prints it. A value that is a
reference to another resource is not followed: its type is `0x01` and its
data is the id it names, so call `values` again with that id.

## Signing

The signing information (Python `apk.signing`, Go `apk.Signing()`, Java
`apk.signing()`) tells you which signature schemes are present and which
certificates signed the APK. It never fails: an unsigned APK gives a value
whose `is_signed` (Go: `IsSigned()`, Java: `isSigned()`) is false, with no
certificates.

<!-- example: apk_metadata.signing -->

`v1` is JAR signing (the `META-INF/*.RSA`, `*.DSA` or `*.EC` files). `v2` and
`v3` are the APK Signature Schemes; `v3` also covers v3.1. A certificate that
appears in several schemes is listed once.

!!! warning "Identification only"
    dex++ reads the certificates. It does **not** check that the signatures
    are valid or that the file's content matches its digests. Use Android's
    `apksigner verify` when you need to know that an APK is correctly signed.

## Archive entries

<!-- example: apk_metadata.entries -->

`entries()` (Go: `Entries()`) returns the name of every entry, in the order of
the archive's table of contents (the ZIP central directory). Each call reads
the whole table of contents again, and a large app can have thousands of
entries, so keep the list when you need it more than once.

`read(name)` (Go: `Read(name)`) returns the uncompressed bytes of one entry.
When the archive has no entry with that name, there are no bytes: `None` in
Python, `false` as the second result in Go, an empty `Optional` in Java.

## Limits

- ZIP64 archives and encrypted entries are not supported.
- Entries must be stored or deflate-compressed.
- When a name appears more than once in the archive, the first entry is used.
