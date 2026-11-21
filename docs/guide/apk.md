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
| `readPermission`, on a provider | `read_permission_reference` | `ReadPermissionReference` | `readPermissionReference()` |
| `writePermission`, on a provider | `write_permission_reference` | `WritePermissionReference` | `writePermissionReference()` |
| `permission`, on `<application>` | `application_permission_reference` | `ApplicationPermissionReference` | `applicationPermissionReference()` |

The reference is absent for a plain value or a missing attribute.

A reference can fail to resolve: the APK has no resource table, the table has
no such id, the value has a type the attribute cannot use (a string where a
number is expected), or the attribute names a theme attribute (`?attr/...`),
whose value depends on the theme the app runs with. Then dexpp cannot tell the
value. The value is absent, the reference still holds the id, and a third
attribute, whose name ends in `unknown`, is true. Each attribute in the table
above has one: `exported_unknown`, `permission_unknown`, `debuggable_unknown`
and so on (Go: `ExportedUnknown`, `PermissionUnknown`, `DebuggableUnknown`;
Java: `exportedUnknown()`, `permissionUnknown()`, `debuggableUnknown()`).

A reference can also lead to a value that Android reads as "not written" (see
[Null values](#null-values)). Then the value is absent too, but `unknown` is
false. So these cases stay apart:

- value absent, reference absent: the attribute is not in the manifest;
- value absent, reference set, `unknown` false: the attribute is in the
  manifest, and Android reads it as not written;
- value absent, reference set, `unknown` true: the attribute is in the
  manifest, but dexpp cannot tell its value.

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
  `debuggableVaries()`, `allowBackupVaries()`). A configuration whose value
  is null counts as what Android does when the attribute is not written:
  `exported` takes its default (see [Components](#components)), and
  `debuggable` and `allowBackup` take theirs.
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

Android reads `android:name`, `android:permission`, `android:readPermission`,
`android:writePermission` and `android:versionName` in a way that ignores a
string that changes with the configuration. If the string has a variant for a
language, a screen or any other configuration except the Android version,
Android uses no value at all. dexpp does the same: the value is absent, the
reference is set, and `unknown` is false. A string that changes only with the
Android version is read by Android, but its value then depends on the device,
so dexpp leaves the value absent and sets `unknown`.

This matters most for `android:permission`. A component guarded by
`@string/guard` is only protected when the string has one value. When it
changes with the language, Android ignores it and the component has no
permission of its own. It then takes the permission of `<application>`, when
there is one (see
[Which components can another app reach](#which-components-can-another-app-reach)).
In `refs.apk` there is none, so these components have no permission and stay
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

### Null values

A resource can hold a *null value*: `@null`, or `@empty`. An attribute can also
be written as `@null` directly. Android reads a null value as if the attribute
were not written at all. This holds in the manifest itself and at the end of a
chain of references:

- `exported`: not written, so Android uses its default: the intent filters
  decide, or, for a content provider, the target SDK (see
  [Components](#components)).
- `debuggable` and `allowBackup`: not written, so Android uses their default:
  false for `debuggable`, true for `allowBackup`.
- A protection level: normal, as when the attribute is missing.
- Text, such as `permission`: no value. A component whose own
  `android:permission` is null takes the permission of `<application>`.

`@null` and `@empty` mean the same thing here. Android only tells them apart
in places that the package parser does not use for these attributes.

dexpp follows the same rules. The value is absent, the reference is kept when
the attribute is a reference, and `unknown` is false. `@null` written directly
is not a reference to a resource (its id is 0), so it reads exactly like a
missing attribute, without a reference.

<!-- example: apk_metadata.null_values -->

`NullActivity` refers to `@bool/nothing`, which is `@null`. So the attribute
counts as not written, and the activity has no intent filter: it is not
exported. `NullFilteredActivity` reaches `@null` through another reference,
and it has an intent filter, so it is exported. `ThemeActivity` uses a theme
attribute, whose value dexpp cannot know: `unknown` is true, and the component
counts as possibly exported.

## Components

A component is an activity, a service, a broadcast receiver or a content
provider. Component names are always full class names: a manifest entry such
as `.MainActivity` is completed with the package name.

<!-- example: apk_metadata.components -->

There are two ways to ask whether a component is exported:

- `exported` (Go: `Exported`, Java: `exported()`) is what the manifest says:
  true, false, or absent when the `android:exported` attribute is missing, is
  a null value, or has a value that dexpp cannot tell. For a reference, it is
  the value of the default configuration.
- `is_exported` (Go: `IsExported`, Java: `isExported()`) is the effective
  answer. When the attribute is missing or null, Android uses a default,
  which is `exported_default` (Go: `ExportedDefault`, Java:
  `exportedDefault()`); see below. When the attribute is a reference whose
  value dexpp cannot tell
  (`exported_unknown` is true), `is_exported` is true: the developer set the
  flag, its value is unknown, and for triage "possibly exported" is the safe
  answer. Guessing from the intent filters could hide an exported component.
  When the attribute is a reference whose value varies between
  configurations, `is_exported` is true too.

The default depends on the kind of component:

- An activity, a service or a broadcast receiver is exported when it has at
  least one intent filter. Apps that target Android 12 or newer must write
  `android:exported` on a component with an intent filter, but older apps
  rely on this rule.
- A content provider ignores its intent filters. It is exported when the app
  targets Android 4.1 (API level 16) or older, and private otherwise. The
  target SDK that counts is the one Android knows when it reads
  `<application>`: that of a `<uses-sdk>` element written **before**
  `<application>` (the last one, if there are several). When
  `android:targetSdkVersion` is not written, the target SDK is the
  `android:minSdkVersion`, or 1. When no `<uses-sdk>` comes before
  `<application>`, it is 0, so the provider is exported.

<!-- example: apk_metadata.provider_default -->

`reachable.apk` targets API level 34, so its provider without
`android:exported` is private, although it has an intent filter. When dexpp
cannot tell the target SDK (a reference it cannot resolve, a codename such as
`"Baklava"`, or a value that differs between configurations), the default is
absent and `is_exported` is true: the provider counts as possibly exported.

## Which components can another app reach

Exported is not the same as reachable. An exported component can still be
protected by a permission that no other app can get.

The permission that protects a component is `permission` (Go: `Permission`,
Java: `permission()`), the one Android enforces. It is the component's own
`android:permission`. When the component gives none, it is the
`android:permission` of the `<application>` element: Android applies that one
to every activity, service, broadcast receiver and content provider that does
not set its own. `permission_inherited` (Go: `PermissionInherited`, Java:
`permissionInherited()`) is true when the permission comes from
`<application>`, and the manifest's `application_permission` (Go:
`ApplicationPermission`, Java: `applicationPermission()`) gives it.

`is_reachable` (Go: `IsReachable`, Java: `isReachable()`) is true when the
component is exported **and** it is not guarded by a signature-level
permission that the app itself declares. A permission with the `signature` or
`signatureOrSystem` protection level is only granted to apps signed with the
same key, so a third-party app cannot hold it.

<!-- example: apk_metadata.reachable -->

In `reachable.apk`, `<application>` requires a signature permission. So the
components that set no permission of their own are protected too, although
they are exported. The reachable ones set a weaker permission of their own,
or are an activity alias without a permission (see below).

### Activity aliases

An `<activity-alias>` element gives an activity a second name, often to put a
second icon in the launcher. Another app starts the alias by its name, and
Android then runs the target activity. dexpp lists each alias as an activity,
in manifest order, with `target_activity` (Go: `TargetActivity`, Java:
`targetActivity()`) set to the class of the activity it starts. For every
other component, `target_activity` is absent. So the `name` of an alias is
not a class; use `target_activity` to find its code.

Android gives an alias only what the alias element itself says about access:

- its own `android:permission`. The alias does not take its target's
  permission, and it does not take the permission of `<application>` either.
  An alias without a permission is open to every app, even when its target is
  guarded by a signature permission.
- its own `android:exported`, or, when that is not written, its own intent
  filters.

<!-- example: apk_metadata.aliases -->

`OpenAlias` starts `SigActivity`, which is guarded by a signature permission,
but the alias itself has no permission, so any app can reach the activity
through it.

### Content providers

A content provider has two permissions: one to read from it and one to write
to it. Android takes the read permission from `android:readPermission`, else
from the provider's `android:permission`, else from the application's. It
takes the write permission the same way, from `android:writePermission`
first. They are `read_permission` and `write_permission` (Go:
`ReadPermission`, `WritePermission`; Java: `readPermission()`,
`writePermission()`); for the other kinds of component they are absent.
Another app reaches a provider when it can read **or** write, so a provider
counts as guarded only when both permissions are signature permissions.

<!-- example: apk_metadata.providers -->

A provider can also have `<path-permission>` elements. Each one names a path,
or a pattern of paths, and the permissions that let a caller read or write
the paths that match. A caller that holds such a permission can use those
paths even without the provider's own permission. So a path permission opens
more of the provider; it never makes the provider safer. dexpp gives them as
`path_permissions` (Go: `PathPermissions`, Java: `pathPermissions()`), in
manifest order. Each has a `path`, a `match` that says which attribute gave
the path (`path`, `pathPrefix`, `pathPattern`, `pathSuffix` or
`pathAdvancedPattern`), and a `read_permission` and `write_permission`. They
come from `android:readPermission` and `android:writePermission`, else from
the element's own `android:permission`; they never fall back to the
provider's permissions. Android drops a `<path-permission>` that gives no
permission or no path, and dexpp leaves those out too.

A provider counts as guarded only when its read and write permissions, and
every permission of its path permissions, are signature permissions.

<!-- example: apk_metadata.path_permissions -->

`PathProvider` takes the signature permission of `<application>`, but its
path `/public` can be read with a normal permission, so it is reachable.

When a provider has no permission of its own, a path permission does close
the matching paths to callers without it. But every other path stays open,
so the provider is not guarded.

Some cases stay reachable on purpose:

- A component guarded by a `normal` or `dangerous` permission. Any app can
  request those.
- A component guarded by a permission the manifest does not declare, such as a
  platform permission. dex++ cannot know its protection level from this APK
  alone, so it does not assume the component is protected.
- A component whose permission dexpp cannot tell (`permission_unknown`, see
  [Text given by a reference](#text-given-by-a-reference)), or whose
  permission has a protection level that is below signature in some
  configuration.
- A provider with a path permission whose permission dexpp cannot tell.

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
