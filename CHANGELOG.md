# Changelog

## Unreleased

### Exported providers, path permissions and activity aliases

- A content provider without `android:exported` is now exported when the app
  targets Android 4.1 (API 16) or older, whatever its intent filters, as
  Android's package parser does (`ParsedProviderUtils.parseProvider`:
  `targetSdkVersion < JELLY_BEAN_MR1`). Before, the intent-filter rule of the
  other components was used, so a provider of an old app with no intent
  filter was reported as not exported, and a provider of a recent app with an
  intent filter as exported. The target SDK is the one Android has when it
  reads `<application>`: only a `<uses-sdk>` before `<application>` counts,
  a missing `targetSdkVersion` means the `minSdkVersion` (or 1), and no
  `<uses-sdk>` means 0. When dexpp cannot tell the target SDK, the provider
  counts as possibly exported.
    - New: C++ `Component::exported_default`, what Android uses when
      `android:exported` gives no value (for the other kinds, whether the
      component has an intent filter). `is_exported()` uses it, and
      `exported_varies` counts a null value as it.
- A provider's `<path-permission>` elements are now read. A caller that holds
  a path's permission reaches the matching paths without the provider's own
  permission, so a provider now counts as guarded only when its read and
  write permissions and every read and write permission of its paths are
  signature permissions. Before, a provider guarded by a signature permission
  but open on some path through a weaker one was reported as not reachable.
    - New: C++ `Component::path_permissions`, a list of `PathPermission`
      (`path`, `match`, `path_unknown`, `read_permission`,
      `read_permission_unknown`, `write_permission`,
      `write_permission_unknown`). Elements Android drops (no permission, or
      no path) are left out.
- `<activity-alias>` elements are now listed with the activities, in manifest
  order. Android gives an alias only its own `android:permission` (not its
  target's, and not the application's), its own `android:exported` and its
  own intent filters, so an alias can open a guarded activity to every app.
  Before, aliases were missing from `components()`.
    - New: C++ `Component::target_activity`, the activity an alias starts
      (unset for every other component).
    - Breaking: `components_of(Activity)` and `components()` now include
      aliases, whose `name` is not a class, and `launcher_activity()` can be
      an alias's name.
- The test fixture `reachable.apk` has two aliases and three more providers.
- The bindings have the new members: Python `Component.exported_default`,
  `target_activity`, `path_permissions` and the classes `PathPermission` and
  `PathMatch`; C `DexppComponent.exported_default`, `target_activity`,
  `path_permissions`, `path_permission_count`, the struct
  `DexppPathPermission` and the enum `DexppPathMatch`; Go
  `Component.ExportedDefault`, `TargetActivity`, `PathPermissions`, the type
  `PathPermission` and `PathMatch` with its constants; Java
  `Component.exportedDefault()`, `targetActivity()`, `pathPermissions()`, the
  record `PathPermission` and the enum `PathMatch`.
    - Breaking, C and Java: `DexppComponent` has new members, so its layout
      changed; the Java record `Component` has new components.

### Permissions and null values in the manifest

- The `android:permission` of `<application>` now guards every activity,
  service, receiver and provider that gives no permission of its own, as
  Android's package parser does (`ParsingPackageUtils`, `ParsedActivityUtils`,
  `ParsedServiceUtils`, `ParsedProviderUtils`). Before, it was ignored, so a
  component guarded only by the application's signature permission was
  reported as reachable.
    - Breaking: a component's `permission` is now the permission Android
      enforces: its own `android:permission`, or the application's when it
      gives none. A component "gives none" when the attribute is missing or
      Android reads it as not written (a null value, or a string that changes
      with the configuration).
    - New: where the permission comes from, and the application's
      permission. C++ `Component::permission_inherited`,
      `Manifest::application_permission()`,
      `application_permission_reference()`, `application_permission_unknown()`;
      Python and C `permission_inherited`, `application_permission`,
      `application_permission_reference`, `application_permission_unknown`;
      Go `PermissionInherited`, `ApplicationPermission`,
      `ApplicationPermissionReference`, `ApplicationPermissionUnknown`; Java
      `permissionInherited()`, `applicationPermission()`,
      `applicationPermissionReference()`, `applicationPermissionUnknown()`.
- A content provider now has a read and a write permission, as Android gives
  it: `android:readPermission` (or `android:writePermission`), else the
  provider's `android:permission`, else the application's. A provider counts
  as guarded only when both are signature permissions, because an app that
  can read or write reaches it. Before, only `android:permission` was read, so
  a provider with a signature `android:permission` and a weaker read or write
  permission was reported as not reachable.
    - New: C++ `Component::read_permission`, `read_permission_reference`,
      `read_permission_unknown`, `write_permission`,
      `write_permission_reference`, `write_permission_unknown` (the same names
      in Python and C; Go `ReadPermission`, `ReadPermissionReference`,
      `ReadPermissionUnknown`, `WritePermission`, ...; Java
      `readPermission()`, ...). They are absent for the other kinds of
      component.
- A null value now reads as an attribute that is not written, as on Android:
  `@null` or `@empty` written in the manifest, or a reference whose resource
  is `@null` or `@empty`, directly or through other references. Android's
  attribute resolution turns `@null` into a null value, `TypedArray.hasValue`
  is false for it, and the other `TypedArray` getters give their default.
  Before, a reference to such a resource counted as unknown, so `is_exported`
  was true. Now the intent filters decide `exported`, `debuggable` and
  `allowBackup` take their default, a protection level is normal, and a
  component's own permission falls back to the application's. The reference
  id is kept; `@null` written directly (a reference to id 0) reads like a
  missing attribute.
    - Breaking: a value that is absent while its reference is set no longer
      always means "unknown". A new member tells the two cases apart for every
      attribute that has a reference member: C++ `Component::name_unknown`,
      `exported_unknown`, `permission_unknown`, `read_permission_unknown`,
      `write_permission_unknown`, `Manifest::version_code_unknown()`,
      `version_name_unknown()`, `min_sdk_unknown()`, `target_sdk_unknown()`,
      `debuggable_unknown()`, `allow_backup_unknown()`,
      `application_permission_unknown()`; the same names in Python and C,
      `...Unknown` in Go and `...Unknown()` in Java. `is_exported` uses
      `exported_unknown` now, not the reference. A text value that differs
      only between Android versions is unknown too.
    - `exported_varies` is now true when the component is exported in some
      configurations and not in others; a configuration whose value is null
      uses the intent-filter rule. `debuggable_varies` and
      `allow_backup_varies` count a null value as the attribute's default.
- Breaking, C and Java: `DexppComponent` and `DexppManifest` have new
  members, so their layout changed; the Java records `Component` and
  `Manifest` have new components.

### Manifest values given indirectly

- A resource can have a different value in each configuration (language,
  screen, Android version). The manifest view now looks at every
  configuration of a referenced resource, not only the default one, so an app
  can no longer hide an exported component behind a `res/values-v31` variant.
    - A boolean attribute still holds the default configuration's value. A new
      member says when another configuration gives another value: C++
      `Component::exported_varies`, `Manifest::debuggable_varies()`,
      `Manifest::allow_backup_varies()`; Python `exported_varies`,
      `debuggable_varies`, `allow_backup_varies`; C `exported_varies`,
      `debuggable_varies`, `allow_backup_varies`; Go `ExportedVaries`,
      `DebuggableVaries`, `AllowBackupVaries`; Java `exportedVaries()`,
      `debuggableVaries()`, `allowBackupVaries()`.
    - `is_exported` (and so `is_reachable`) is now true when `exported`
      varies: the component is exported on the devices that match a
      configuration that says so.
    - A protection level given by a reference works the same way:
      `Manifest::declared_permission_protection_level_varies(name)` (Python
      `declared_permission_protection_level_varies`, C
      `DexppDeclaredPermission.protection_level_varies`, Go
      `DeclaredPermission.ProtectionLevelVaries` and
      `Manifest.DeclaredPermissionProtectionLevelVaries`, Java
      `DeclaredPermission.protectionLevelVaries()` and
      `Manifest.declaredPermissionProtectionLevelVaries`). A permission whose
      level is below signature in some configuration no longer guards a
      component.
    - The integers (version code, SDK levels) keep the default
      configuration's value.
- Text where a boolean is expected is now read the way Android reads it
  (`TypedArray.getBoolean` and `XmlUtils.convertValueToBoolean`): exactly
  `"1"`, `"true"` and `"TRUE"` are true, any other text is false, and empty
  text gives the attribute's default (false for `exported` and `debuggable`,
  true for `allowBackup`). This covers a literal string in a hand-made binary
  manifest and a reference to a `<string>` resource. Before, such a value was
  unknown, and an unknown `exported` counted as exported.
    - Breaking: `ResourceTable::resolve_bool` (and `resolve_bool`,
      `dexpp_apk_resolve_bool`, `ResolveBool`, `resolveBool` in the bindings)
      now reads strings with that rule instead of returning no value, and
      reads a float, a dimension or a fraction as false.
- Text attributes given by a reference are now resolved: the `android:name`
  of a component and of a `<permission>`, `android:permission` and
  `android:versionName`. Android reads them with
  `TypedArray.getNonConfigurationString`, which ignores a string that changes
  with the configuration (except the Android version); dexpp does the same,
  and also leaves a string that changes only with the Android version without
  a value. An unresolved or ignored reference gives no value and keeps its id
  in a new member: C++ `Component::name_reference`,
  `Component::permission_reference`, `Manifest::version_name_reference()`
  (Python `name_reference`, `permission_reference`,
  `version_name_reference`; C `name_reference`, `permission_reference`,
  `version_name_reference`; Go `NameReference`, `PermissionReference`,
  `VersionNameReference`; Java `nameReference()`, `permissionReference()`,
  `versionNameReference()`). So a permission named through a reference now
  counts as a guard, and one that Android ignores no longer does.
- These text attributes and the boolean ones are read from the attribute's
  typed value, as Android's `TypedArray` reads them, not from its raw string.
  The two only differ in a hand-made file. `axml::XmlAttribute` has a new
  `typed_string` member with the text of a string-typed value.
- Breaking, C and Java: `DexppComponent`, `DexppDeclaredPermission` and
  `DexppManifest` have new members, so their layout changed; the Java records
  `Component`, `DeclaredPermission` and `Manifest` have new components.

### Resource values in every configuration

- `ResourceTable::values(id)` returns the value of a resource in every
  configuration, as the table stores it: the configuration written like a
  `res/values-...` folder name (`""`, `"v31"`, `"fr"`,
  `"b+sr+Latn-land-night-v8"`), the raw type and data, the text of a string,
  and the value read as a boolean or an integer. A reference is not followed.
    - Python: `ResourceTable.values(id)` and the `ResourceValue` class.
    - C: `dexpp_apk_resource_values()`, `DexppResourceValue` and
      `dexpp_resource_value_array_free()`.
    - Go: `ResourceTable.Values(id)` and `ResourceValue`, with `AsBool()` and
      `AsInt()`.
    - Java: `ResourceTable.values(id)` and the `ResourceValue` record.
- The `resolve_*` lookups now use the first default-configuration value when
  a damaged table holds two, as Android does, instead of the last one.

### Cross-references: faster, and much less memory

- Building the cross-reference index (`xrefs()` in every binding) no longer
  decodes every method into the context's instruction cache, where the
  instructions stayed for the life of the context. It now reads each
  method's code once, picks out the instructions that name a string, a type,
  a field or a method without decoding the others, and keeps nothing but the
  index. The index holds exactly the same results, in the same order. On a
  146 MB app with 16 DEX files, building it took 4.0 s and added 1.9 GB of
  memory; it now takes 0.5 s (1.3 s on one thread) and adds 0.44 GB.
- The index is built on several threads, as the call graph is: each DEX file
  is read on its own thread, and the four tables (strings, types, fields,
  methods) are filled on their own threads. The thread count of the analysis
  (`ApkLoadOptions::threads`, `threads` in Python, C and Java, `WithThreads`
  in Go) controls it, and the index does not depend on it.
- Building the call graph no longer keeps every method's code item in memory
  for the life of the context either: on the same app it now adds 0.39 GB
  instead of 0.54 GB.
- An explicit thread count above 1 is now used by the call graph and the
  cross-references whatever the size of the program. Before, a program whose
  DEX files besides the biggest held less than 1 MiB always stayed on the
  calling thread. The automatic count (0) keeps that rule.

### Readable bytecode

- A method's code can now be read as text with every reference named, in the
  style of smali and dexdump: one line per instruction, with its offset, such
  as `invoke-virtual {v0, v1},
  Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;`,
  `const-string v1, "hello"` or `iget v0, v1, Lcom/example/Foo;->count:I`.
    - Strings are quoted, with quotes, backslashes, control characters and
      invisible characters (such as U+200B or a right-to-left override)
      escaped.
    - A method handle shows its kind and member (`invoke-static@LA;->f()V`),
      a prototype its descriptor, and a call site its method name, method
      type, extra arguments and bootstrap method.
    - Branch targets are code offsets, the same numbers as the offsets of the
      lines; switch and array tables are written out in the instruction that
      uses them.
    - A reference outside its table, in a damaged file, shows as the raw
      index with a question mark (`string@4?`).
    - C++: `Method::disassemble()` and `dex::DisassemblyLine`. Python:
      `Method.disassemble()` and `DisassemblyLine`. C:
      `dexpp_method_disassemble()`, `DexppDisassemblyLine` and
      `dexpp_disassembly_line_array_free()`. Go: `Method.Disassemble()` and
      `DisassemblyLine`. Java: `DexMethod.disassemble()` and the
      `DisassemblyLine` record.
- `to_string` (Go `ToString`, Java `Dexpp.toString`) still writes one
  instruction with raw pool indices, as it has no file to name them from.
- The demo disassembler (`main_test`) now prints the named form, and each
  method's prototype.

## 0.3.1

### APK from memory

- An APK can be opened from its bytes in memory, not only from a file. The
  bytes get the same checks as a file, and everything an opened APK offers
  works the same: archive entries, the manifest, resources, signing and the
  code. The APK keeps its own copy of the bytes. An optional label names the
  bytes at the start of error messages, where a file gives its path.
    - C++: `dex::apk::Apk::from_buffer(bytes, label)`. One overload takes a
      `std::vector<uint8_t>` by value, so a vector passed with `std::move` is
      not copied; the other takes a `std::span<const uint8_t>` and copies it.
    - Python: `dexpp.Apk.from_bytes(data, label="<apk>")`, for any bytes-like
      object, including a non-contiguous `memoryview`.
    - C: `dexpp_apk_open_buffer(bytes, len, label, err)`.
    - Go: `dexpp.OpenAPKBytes(data, label)`.
    - Java: `Apk.fromBytes(data)` and `Apk.fromBytes(data, label)`.
- The APK's `analysis()` now takes a thread count, like loading an APK with
  the analysis context: 1 keeps all the work, the call graph included, on the
  calling thread. The analysis is kept for each thread count: a second call
  with the same count returns the same analysis, and a call with another count
  builds a separate one.
    - C++: `Apk::analysis(const ApkLoadOptions & = {})`. `ApkLoadOptions` now
      has `operator==`.
    - Python: `Apk.analysis(threads=0)`.
    - Go: `Apk.Analysis(opts ...LoadOption)`, with `WithThreads`.
    - Java: `Apk.analysis(int threads)`, next to `analysis()`.
- The errors of the APK's `analysis()` now start with the APK's path (or the
  label), not with `<apk>`.
- Breaking, C: `dexpp_apk_analysis` takes a `uint32_t threads` argument
  before the error argument, as `dexpp_context_from_apk` does. Pass 0 for the
  old behaviour.

## 0.3.0

### Platforms

- dex++ now builds and is tested on macOS (Apple Silicon and Intel, with Apple
  Clang) and on Windows x64 (with MSVC), next to Linux. CI runs the C++ tests
  and the Python tests on all three, and the Go tests on Linux and macOS.
- Releases now publish wheels for macOS 14 or newer (one for arm64, one for
  x86_64) and for Windows x64, next to the Linux ones.
- The Go package links libc++ on macOS. It does not support Windows.
- The Python module is now placed directly in the build folder with every
  CMake generator, so `PYTHONPATH=build` works with Visual Studio too.
- With MSVC, the library is compiled with Microsoft's
  `_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR`. Without it, code built with Visual
  Studio 2022 17.10 or newer crashes the first time it takes a lock when the
  process has loaded an older `msvcp140.dll`. A JVM loads the one in the
  JDK's `bin` folder, and JDK 22 ships an older one; a Python process can
  have loaded an older one too.

### Packaging

- dexpp works with Python 3.10 and newer again (0.2.0 needed 3.14). Releases
  publish wheels for CPython 3.10, 3.11, 3.12, 3.13 and 3.14 on every
  platform: Linux x86_64 (glibc and musl), macOS arm64 and x86_64, and Windows
  x64. CI runs the Python tests on 3.10 and 3.14, the oldest and the newest.
- The package metadata now lists the supported Python versions and macOS and
  Windows, next to Linux.

### Documentation

- dex++ now has a documentation site, <https://burogurama.github.io/dexpp/>:
  guides for APK metadata, code analysis, the call graph, cross-references and
  DEX editing, a Python reference, and a C++ and C reference. It is published
  from each release tag, so it describes the latest release.
- Every class, method and property in the Python type stubs now has a
  docstring, so editors show a description next to each completion.
- The examples in the guides are run by the tests, with the output the guides
  show.
- The quick start, the guides and the home page show every example in Python,
  Go and Java, in tabs; choosing a language switches every example on the
  site. The Go and Java versions are run by `go test` and by the Java tests.

### One vocabulary in every language (breaking)

These renames make C++, Python, C, Go and Java use the same word for the same
thing. The old names are gone; there are no aliases.

- `Class.name` is now `Class.descriptor`. It always returned the class
  descriptor, such as `"Lcom/example/Foo;"`, while `Method.name` and
  `Field.name` are simple names, and every other place calls this string a
  descriptor. The new names are `Class::descriptor()` in C++,
  `Class.descriptor` in Python, `dexpp_class_descriptor` in C,
  `Class.Descriptor()` in Go and `DexClass.descriptor()` in Java.
- `Class.package` now gives the package in Java source form, with dots:
  `"com.example"`, not `"com/example"`. This is the form `Manifest.package`
  uses, so the two can be compared. Before, a test such as
  `cls.package.startswith(manifest.package)` was false for the app's own
  classes whenever the package name has a dot, and nothing said why. The
  default package is still an empty string. This applies to C++
  (`Class::package()`, which now returns a `std::string` instead of a
  `std::string_view`), Python, C (`dexpp_class_package`), Go
  (`Class.Package()`) and Java (`DexClass.packageName()`).
- Two enums are renamed, so that each name says what it is about:
    - `EdgeKind`, the kind of a control-flow-graph edge, is now
      `CfgEdgeKind` in C++ and Python. C, Go and Java already called it
      `DexppCfgEdgeKind` and `CfgEdgeKind`.
    - `TypeRefKind`, what an instruction does with the type it names, is now
      `TypeUseKind`: `dex::TypeUseKind` in C++, `dexpp.TypeUseKind` in
      Python, `DexppTypeUseKind` with the values `DEXPP_TYPE_USE_CONST_CLASS`
      and so on in C, `TypeUseKind` with the constants `TypeUseConstClass`
      and so on in Go, and `TypeUseKind` in Java. The C++ helper
      `type_ref_kind_from_opcode` is no longer public (see "C++ API
      (breaking)"). The member names and numbers do not change.
- A method's prototype, such as `"(Ljava/lang/String;I)V"`, is called `proto`
  everywhere, as in `MethodId.proto`. The accessor that called it a
  descriptor is renamed: `dexpp_method_descriptor` is now
  `dexpp_method_proto` in C, `Method.Descriptor()` is now `Method.Proto()` in
  Go, and `DexMethod.descriptor()` is now `DexMethod.proto()` in Java.
- The model's class summary now holds the list of interfaces itself, instead
  of only their number, and the separate call for the list is gone. Before,
  that call returned an empty list for a class the model does not have, while
  every other lookup reports a missing class as not found. Now the summary is
  the only lookup, and a missing class gives `None` in Python, 0 in C, `false`
  in Go and an empty `Optional` in Java, as before.
    - Python: `ClassInfo.interfaces` (a `list[str]`) replaces
      `ClassInfo.interface_count` and `Model.class_interfaces()`.
    - C: `DexppClassInfo` has a new member `interfaces` (an array of strings),
      and `interface_count` is now its length. `dexpp_class_info_clear` frees
      it. `dexpp_model_class_interfaces` is removed.
    - Go: `ClassInfo.Interfaces` (a `[]string`) replaces
      `ClassInfo.InterfaceCount` and `Model.ClassInterfaces()`.
    - Java: `ClassInfo.interfaces()` (a `List<String>`) replaces
      `ClassInfo.interfaceCount()` and `Model.classInterfaces()`.
- Python's `Model` had two ways to count its classes and two ways to test for
  one. `Model.class_count` and `Model.has_class()` are removed: use
  `len(model)` and `descriptor in model`. C, Go and Java keep their single
  way: `dexpp_model_class_count` and `dexpp_model_has_class`,
  `Model.ClassCount()` and `Model.HasClass()`, `Model.classCount()` and
  `Model.hasClass()`.

### No magic numbers, no raw indices, no hidden cases

These changes make every value say what it is. A number that only meant
something together with a table nobody could read, a special value that
stood for a different case, and one type that held several different cases
are gone. Most of them are breaking; the old names are gone, with no
aliases.

- A method's identity and prototype, and a field's identity, are now one call
  in every language. Go and Java already had `Method.ID()` / `id()`,
  `Method.Proto()` / `proto()` and `Field.ID()` / `id()`; C++ and Python had
  none of them.
    - C++: `Method::id()` returns the `MethodId`, `Method::proto()` the
      prototype (such as `"(Ljava/lang/String;I)V"`), and `Field::id()` the
      `FieldId`.
    - Python: the properties `Method.id`, `Method.proto` and `Field.id`.
    - C: `dexpp_method_id()` and `dexpp_field_id()` fill a `DexppMethodId` or
      a `DexppFieldId`. The new `dexpp_field_id_clear()` frees a
      `DexppFieldId`.
    - Go and Java now get each identity with one call to C, instead of one
      call for each part.
- Breaking: a method handle value (in an annotation, or as a static field's
  initial value) is now resolved. Before, it was only a raw index into one
  DEX file's method handle table, which no public call could resolve. Now it
  has a `kind`, which says what the handle does, and a `member`, the field or
  the method it works on. The kind is a new enum, `MethodHandleKind`, with
  the nine method handle types of the DEX format: `STATIC_PUT`, `STATIC_GET`,
  `INSTANCE_PUT` and `INSTANCE_GET` work on a field (a `FieldId`), and
  `INVOKE_STATIC`, `INVOKE_INSTANCE`, `INVOKE_CONSTRUCTOR`, `INVOKE_DIRECT`
  and `INVOKE_INTERFACE` call a method (a `MethodId`). In C++ the enum is
  `dex::MethodHandleKind`, in `common/method_handle_kind.hpp`: the editable
  model's `model::MethodHandle` now uses the same enum, so the analysis and
  the model share one vocabulary (code that named it
  `dex::model::MethodHandleKind` now names `dex::MethodHandleKind`).
    - C: the enum `DexppMethodHandleKind` and the member
      `DexppValue.method_handle_kind` replace `method_handle_index`; the
      member is in `field` or `method`.
    - Go: `MethodHandleValue.Kind` (a `MethodHandleKind`) and
      `MethodHandleValue.Member` (a `MemberID`: a `MethodID` or a `FieldID`)
      replace `Index`.
    - Java: `MethodHandleValue.kind()` and `MethodHandleValue.member()` (a
      `MemberId`, the new sealed interface of `MethodId` and `FieldId`)
      replace `index()`.
- Breaking: the annotation value classes are renamed so that they do not
  look like handles or cross-references. They are plain values:
  `TypeRef` is now `TypeValue`, `EnumRef` is `EnumValue`, `FieldRef` is
  `FieldValue`, `MethodRef` is `MethodValue`, `MethodTypeRef` is
  `MethodTypeValue` and `MethodHandleRef` is `MethodHandleValue`. This
  applies to the nested types of `dex::AnnotationValue` in C++, the Python
  classes, the Go types and the Java records. C keeps its `DEXPP_VALUE_*`
  kinds.
- Breaking: decoded instructions no longer hide distinct cases. There are now
  28 instruction types instead of 24:
    - `MoveInstruction.src` is optional. It is empty (`std::nullopt` in C++,
      `None` in Python, `HasSrc` false in Go, an empty `OptionalInt` in Java,
      `has_src` 0 in C) for `move-result*` and `move-exception`, which name no
      source register. Before, it was 0, which is also the real register
      `v0`.
    - `invoke-polymorphic` and `invoke-polymorphic/range` decode to a new
      `InvokePolymorphicInstruction`, with the method index, the index of the
      call site's prototype, and the argument registers. Before, they were a
      plain `InvokeInstruction` and the prototype index was lost. They stay
      call sites: the call graph, `Method.calls()` and the cross-references
      list them with the kind `POLYMORPHIC`, as before.
    - `array-length` decodes to a new `ArrayLengthInstruction` (`dest` and
      `array_reg`). Before, it was a `UnaryOpInstruction`, which nothing said.
    - An opcode the format does not use decodes to a new `InvalidInstruction`,
      whose opcode is the raw byte. Before, it was a `NopInstruction`, so a
      damaged method looked fine. The control-flow graph still treats it
      like a `nop`.
    - `ConstHandleInstruction`, whose `index` pointed into one of two tables
      depending on the opcode, is split into `ConstMethodHandleInstruction`
      (`method_handle_index`) and `ConstMethodTypeInstruction`
      (`proto_index`). `to_string` now writes these operands as
      `method_handle@N` and `proto@N`, instead of `idx@N`.

  In C, the instruction kinds are `DEXPP_INSN_CONST_METHOD_HANDLE` (11, which
  was `DEXPP_INSN_CONST_HANDLE`), `DEXPP_INSN_CONST_METHOD_TYPE` (24),
  `DEXPP_INSN_INVOKE_POLYMORPHIC` (25), `DEXPP_INSN_ARRAY_LENGTH` (26) and
  `DEXPP_INSN_INVALID` (27); `DexppInstruction` has the members
  `method_handle_index` and `proto_index` instead of `index`, and `has_src`
  now also applies to `DEXPP_INSN_MOVE`. Go and Java have the same five types
  and records.
- Breaking: a control-flow-graph edge no longer has a `label`. The label was
  one optional integer with two meanings: the case key of a switch-case edge,
  or, on an exception edge, the caught type as a raw index into one DEX
  file's type table, with 4294967295 for a handler that catches everything.
  No public call could turn that index into a name. Now:
    - `case_key` is the case key, set only for `SWITCH_CASE` edges.
    - `catch_type` is the descriptor of the caught exception class, such as
      `"Ljava/io/IOException;"`, set only for `EXCEPTION` edges.
    - A handler that catches everything (how Java compiles `finally`) gets
      edges of a new kind, `EXCEPTION_CATCH_ALL`, with no catch type.
      `EXCEPTION` edges now always go to a handler of one class. A switch
      already worked this way: `SWITCH_DEFAULT` is its own kind, apart from
      `SWITCH_CASE`.

  In C++ these are `CfgEdge::case_key` (`std::optional<int32_t>`),
  `CfgEdge::catch_type` (`std::optional<std::string_view>`, valid as long as
  the context) and `CfgEdgeKind::ExceptionCatchAll`. In Python,
  `CfgEdge.case_key`, `CfgEdge.catch_type` and
  `CfgEdgeKind.EXCEPTION_CATCH_ALL`. In C, `DexppCfgEdge.case_key`,
  `DexppCfgEdge.catch_type` (a string the edge owns, freed by
  `dexpp_basic_block_array_free`) and `DEXPP_CFG_EXCEPTION_CATCH_ALL` replace
  `has_label` and `label`. In Go, `CfgEdge.CaseKey`, `CfgEdge.CatchType` and
  `CfgExceptionCatchAll` replace `Label` and `HasLabel`. In Java,
  `CfgEdge.caseKey()` (an `OptionalInt`), `CfgEdge.catchType()` (an
  `Optional<String>`) and `CfgEdgeKind.EXCEPTION_CATCH_ALL` replace `label()`.
- Breaking: a call-graph node now has a `kind` instead of the `resolved`
  flag. The flag mixed two different cases: an `invoke-custom` call site was
  an unresolved node, like a framework method, with a made-up identity of the
  form `("call_site", "#<dex>:<index>", "")` that looked like a method. The
  kind is `DEFINED` (a method defined in a loaded file), `EXTERNAL` (a method
  the code calls but no loaded file defines) or `CALL_SITE` (an
  `invoke-custom` call site). The identity of a `CALL_SITE` node is opaque: it
  only keeps call sites apart, and it may change.
    - C++: the enum `dex::CallNodeKind` and `CallNode::kind` replace
      `CallNode::resolved`.
    - Python: the enum `dexpp.CallNodeKind` and `CallNode.kind`.
    - C: the enum `DexppCallNodeKind` (`DEXPP_CALL_NODE_DEFINED`,
      `DEXPP_CALL_NODE_EXTERNAL`, `DEXPP_CALL_NODE_CALL_SITE`) and the member
      `DexppCallNode.kind` replace `DexppCallNode.resolved`.
    - Go: the type `CallNodeKind` (`CallNodeDefined`, `CallNodeExternal`,
      `CallNodeCallSite`) and the field `CallNode.Kind` replace
      `CallNode.Resolved`.
    - Java: the enum `CallNodeKind` and `CallNode.kind()` replace
      `CallNode.resolved()`.

### Python bindings

- The enums `InvokeKind`, `EdgeOrigin`, `CfgEdgeKind`, `TypeUseKind`,
  `OperandWidth`, `AnnotationVisibility` and `ComponentKind` are now real
  Python enums, subclasses of `enum.Enum`, as the type stubs already said.
  `list(dexpp.InvokeKind)` lists the members, `isinstance(kind, enum.Enum)` is
  true, and the members can be pickled. The member names do not change, and
  `kind.name`, `kind.value`, `==` and hashing work as before. Breaking: a member
  is no longer a number, so `int(kind)` raises `TypeError`. Use `kind.value`,
  which gives the same number `int(kind)` gave. Passing anything other than a
  member of the right enum to `CallEdgeView.where` or `Manifest.components_of`
  raises `TypeError`.
- Every function and method now takes its arguments by keyword, under the
  names the type stubs give them. Before, 25 of them took positional arguments
  only, so a call such as `ctx.find_class(descriptor="LFoo;")` or
  `xrefs.string_refs(value="...")` passed the type checker and then failed with
  `TypeError`. The type stubs now also say that `Model.read` takes any
  bytes-like object (`bytes`, `bytearray`, `memoryview`), which it always
  accepted.
- `Class`, `Method`, `Field` and `ClassRef` objects now compare equal when they
  point at the same class, method or field of the same analysis, and they can
  be dictionary keys and set members. Before, `ctx.find_class("LA;") ==
  ctx.find_class("LA;")` was `False`, because each call makes a new Python
  object, and a `set` of methods did not remove duplicates. The same file
  loaded twice gives two analyses, and their objects are not equal.
- The annotation value classes (`TypeRef`, `EnumRef`, `FieldRef`,
  `MethodRef`, `MethodTypeRef` and `MethodHandleRef`, now renamed; see "No
  magic numbers, no raw indices, no hidden cases") and `ResourceName` could
  be compared but not hashed, so they could not be dictionary keys or set
  members. They now have a hash that matches their equality.
- Breaking: thirteen properties that do a lot of work on every read are now
  methods, so they need `()`: `AnalysisContext.classes()` (makes a new `Class`
  object for every class of every DEX file), `AnalysisContext.strings()`
  (copies every string of every DEX file), `Xrefs.referenced_strings()`,
  `CallNode.callers()` and `CallNode.callees()` (collect, deduplicate and sort
  the nodes again; for a method called from many places, `callers()` goes
  through every one of those call sites; both still return a `CallNodeView`),
  `Method.calls()`, `Method.field_accesses()`, `Method.string_loads()` and
  `Method.type_uses()` (go through the method's bytecode again and build new
  objects; a loop such as `for i in range(len(m.calls)): m.calls[i]` went
  through the method once per step), `Apk.entries()` (reads the whole ZIP
  central directory again), `Model.class_descriptors()` (copies every
  descriptor), `Model.required_version()` (runs the whole validator) and
  `Model.pool_stats()` (walks the whole model). The rule, written in the
  Python reference, is that a property is cheap or cached and a method
  computes. Code that still reads one of them without `()` gets the bound
  method instead of the result.
- `Method.instructions` is now a `dexpp.InstructionView`, a read-only
  sequence over the decoded bytecode, like `Cfg.blocks`. It copies nothing up
  front; before, every read of the property built a new list of every
  instruction. Indexing, slicing (which returns a `list`), `len()` and
  iteration work as before. Code that changed the list in place, or compared it
  with a list, must call `list(method.instructions)` first.
- `access_flags` (on `Class`, `Method`, `Field` and `ClassInfo`) is now a
  `dexpp.AccessFlags`, an `enum.IntFlag` with one member per DEX flag:
  `dexpp.AccessFlags.STATIC in method.access_flags`. It is still an `int`, so
  code that compared it with numbers keeps working.
- dexpp now raises its own exceptions, one per kind of error, where it raised
  a plain `ValueError` before:

  | Exception | Raised when |
  | --------- | ----------- |
  | `InvalidApkError` | a file is not a ZIP archive, or an APK has no `classes.dex` |
  | `InvalidDexError` | a DEX file cannot be parsed |
  | `UnsupportedDexError` | `Model.read` gets a format it does not read (DEX version 41) |
  | `InvalidModelError` | `Model.write` gets a model that breaks rules of the format; its `problems` attribute lists them |
  | `PoolOverflowError` | `Model.write` would need a table with more entries than the format allows |
  | `VersionTooLowError` | `Model.write` gets a model that needs a higher DEX version |
  | `ModelError` | `Model.copy_class` or `Model.move_class` cannot find the class, or the destination already has it; also the base class of the three above |

  All of them derive from `dexpp.Error`, and `dexpp.Error` derives from
  `ValueError`, so code that catches `ValueError` keeps working.
- `Model.read_file` and `Model.write_file` raised `FileNotFoundError` for every
  file problem. `read_file` now raises it only when the file does not exist,
  and `OSError` when the file exists but cannot be read. `write_file` raises
  `OSError` when the file cannot be created or written (breaking for code that
  caught `FileNotFoundError` around `write_file`).
- `AnalysisContext.from_dex`, `from_dex_files`, `from_apk` and `Apk.open` now
  raise `OSError` for a path that exists but cannot be read. A directory path
  made them raise `MemoryError` before (see "Fixes").
- `dexpp.Instruction` and `dexpp.AnnotationValue` now exist in the module. The
  type stubs already declared these two unions, but the module did not have
  them, so `isinstance(x, dexpp.Instruction)` raised `AttributeError`.

### C and Go bindings

- The C API (`bindings/c/dexpp.h`) now covers the whole analysis API, the same
  ground as the Python binding. Before, it had loading, classes, the names,
  descriptors and flags of methods, the string table, and the manifest's
  package and components. New: fields and class references, decoded
  instructions with `dexpp_opcode_name` and `dexpp_instruction_to_string`,
  control-flow graphs, try blocks, annotations with typed values and the
  initial values of static fields, a method's calls, field accesses, string
  loads, type uses and parameter registers, the call graph, the class
  hierarchy, the cross-reference index, and the whole manifest, resources,
  archive entries and signing information of an APK. The header's first
  comment gives the rules for memory, arrays and threads.
- **Breaking for C callers:** every function that can fail now takes a
  `DexppError **err` instead of a `char **err`. The error object gives a code
  (`dexpp_error_code`, one value per kind of failure, matching the Python
  exceptions), a message (`dexpp_error_message`) and, for a model that breaks
  rules of the DEX format, the list of problems
  (`dexpp_error_problem_count`, `dexpp_error_problem_where`,
  `dexpp_error_problem_message`). Free it with `dexpp_error_free`.
- Other changes that break C callers: `dexpp_context_from_apk` takes the
  number of threads; `dexpp_class_superclass` and `dexpp_class_interfaces`
  return `DexppClassRef` handles instead of strings; `dexpp_context_strings`
  returns `DexppText`, which keeps NUL bytes; `dexpp_method_call_count` is
  replaced by `dexpp_method_calls`; `dexpp_apk_components` is replaced by
  `dexpp_apk_manifest`; and `dexpp_class_info_free` is renamed
  `dexpp_class_info_clear`.
- The Go package now reaches the whole analysis API too: `Field`, `ClassRef`,
  `Method.Instructions` (an `Instruction` interface with one struct type per
  opcode family), `OpcodeName`, `ToString`, `Method.Cfg`, `Method.TryBlocks`,
  `Annotations` and `Field.InitialValue`, `Method.Calls`, `FieldAccesses`,
  `StringLoads`, `TypeUses` and `ParameterRegisters`, `CallGraph`,
  `ClassHierarchy`, `Xrefs`, `FromDEXFiles`, the `WithThreads` option of
  `FromAPK`, the whole `Manifest` with intent filters, `ResourceTable`,
  `Apk.Entries`, `Apk.Read`, `Apk.Signing` and `Version`.
- Go errors are now `*dexpp.Error` values with a `Code`, a `Message` and, for
  an invalid model, the `Problems`. Each code has a sentinel, such as
  `dexpp.ErrInvalidDex`, for `errors.Is`, and `errors.Is(err, fs.ErrNotExist)`
  is true for a missing file. Before, errors were plain strings.
- Changed Go signatures (breaking):
    - `Class.Superclass()` returns a `*ClassRef`, which is nil for
      `java.lang.Object`, instead of a string that was empty when there was
      no superclass. `Class.Interfaces()` returns `[]*ClassRef` instead of
      `[]string`.
    - The component kind constants are renamed: `Activity`, `Service`,
      `Receiver` and `Provider` are now `ComponentActivity`,
      `ComponentService`, `ComponentReceiver` and `ComponentProvider`.
    - `Apk.Components()` is replaced by `Apk.Manifest()`, which returns the
      whole manifest. In `Component`, `Exported` is now the attribute as the
      manifest writes it (a `*bool`, nil when missing), `IsExported` is the
      effective answer, and `Reachable` is renamed `IsReachable`.
    - `Apk.Package()` returns `(string, bool)`, false when the APK has no
      manifest, instead of an empty string.
    - `Class.AccessFlags()`, `Method.AccessFlags()` and
      `ClassInfo.AccessFlags` are a `dexpp.AccessFlags` instead of a `uint32`.
    - `Method.CallCount()` is removed. Its comment said it counted resolved
      calls, but it counted every invoke instruction. `len(m.Calls())` gives
      the same number.
- Go: calling a method on a closed or nil handle now panics with a message
  that names the handle type, instead of passing a null pointer to C.
  `Close()` on a nil handle does nothing.
- The C API can now be built as one shared library, `dexpp_c_shared`
  (`libdexpp_c_shared.so`, `libdexpp_c_shared.dylib` or
  `dexpp_c_shared.dll`), with the CMake option `-DDEXPP_BUILD_C_SHARED=ON`.
  It holds the C layer and the C++ library, and exports the `dexpp_`
  functions and nothing else. Every declaration in the C headers is marked
  with the new `DEXPP_C_API` macro for this; it is empty in every other
  build. The static `dexpp_c` that Go links does not change.
- The `_array_free` functions of handle arrays (classes, class references,
  methods, fields) are documented to free the array alone when the count is
  0, so a caller can keep the handles. Use this instead of `free()` with the
  shared library: on Windows, the DLL and the program can use different C
  runtimes.

### Java binding (new)

- dex++ has a Java package, `io.github.burogurama.dexpp`, in
  `bindings/java`, built with Maven. It needs Java 22 or newer: it calls the
  C API from the shared library through the Foreign Function & Memory API,
  with no JNI code. It reaches the same API as the Go package: loading DEX
  files and APKs, classes, methods and fields, decoded bytecode, control-flow
  graphs, try blocks, annotations, the call graph, the class hierarchy,
  cross-references, the manifest, resources and signing of an APK, and the
  editable model a whole class at a time. It works on Linux, macOS and
  Windows, from a checkout; it is not on Maven Central yet.
- Handles (`AnalysisContext`, `Apk`, `DexClass`, `ClassRef`, `DexMethod`,
  `DexField`, `CallGraph`, `ClassHierarchy`, `Xrefs`, `Model`) are
  `AutoCloseable`, and a `Cleaner` frees the ones that are never closed.
  Everything else is copied into Java records and immutable lists. A value
  that may be absent is an `Optional`, `OptionalInt` or `OptionalLong`.
- Instructions are a sealed interface with one record per kind, and
  annotation values are a sealed interface too, so a pattern-matching
  `switch` tells them apart. Access flags are an `AccessFlags` record with
  one constant per flag.
- A file that cannot be opened throws `NoSuchFileException` or
  `FileSystemException`. Every other error throws an unchecked
  `DexppException` that carries the C error code. Its subclasses match the
  Python exceptions: `InvalidApkException`, `InvalidDexException`,
  `UnsupportedDexException`, `ModelException`, and below it
  `InvalidModelException` (with the list of problems),
  `PoolOverflowException` and `VersionTooLowException`.
- The documentation site has a Java page, with examples that the tests run.
  CI builds the shared library and runs the Java tests on Linux, macOS and
  Windows, with Java 22 and Java 25.

### Fixes

- A damaged APK Signing Block could make `parse_signing_info` (and so
  `Apk::signing()` / `Apk.signing`) read outside the APK's memory, or loop
  forever. Two size checks were written as `a + b > c`, and a huge size taken
  from the file wrapped around and passed them. The fuzzer found it; the checks
  now compare by subtraction.
- The manifest now reads the `android:` attributes by resource id, as Android
  does, instead of by name. Before, an app could hide `android:exported` (and
  so change `is_exported` and `is_reachable`) by renaming the attribute's name
  string, or add an attribute that is only named `exported` and have dexpp
  read it although Android ignores it. The binary-XML decoder now gives each
  attribute its `resource_id` and each document `has_resource_map`, and
  `XmlNode::attribute_by_id()` finds an attribute by id. For a document
  without a resource map (not written by aapt or aapt2), the manifest falls
  back to the attribute name in the android namespace.
- Go: the short methods of `Class` and `Method` (`Name`, `Package`,
  `Superclass`, `AccessFlags` and others), and the early-return paths of
  `Classes`, `Strings`, `Interfaces`, `Methods` and `Components`, did not keep
  their handle alive during the C call. In code such as
  `ctx.FindClass(d).Name()`, the garbage collector could free the handle while
  C was still using it. Every method now keeps its handle alive until it
  returns.
- Python: some small value objects could read freed memory. These strings
  pointed into the analysis's memory: `StringLoad.value`, `TypeUse.descriptor`,
  `CatchHandler.type_descriptor` (in `TryBlock.handlers`),
  `ParameterRegister.type`, and `CfgEdge.catch_type` (in
  `BasicBlock.successors`). But these objects do not keep the analysis alive.
  So once the last `AnalysisContext` and every handle from it were gone,
  reading one of these strings gave garbage, raised `UnicodeDecodeError`, or
  crashed. It happened to the results of `string_loads()`, `type_uses()`,
  `try_blocks` and `parameter_registers`, and to every `BasicBlock` from
  `Cfg.blocks`, `Cfg.entry` and `Cfg.block_at_offset()`. These objects now
  hold their own copy of each string, made when the object is created, so
  they stay valid on their own. The Python API does not change. C, Go and
  Java were not affected: they always copied the strings.
- `Model.read` in Python read the wrong memory when it got a buffer whose bytes
  are not next to each other, such as a reversed or strided `memoryview`: it
  read forward from the buffer's start and could go past its end. It now reads
  the bytes in order, copying them first when the buffer is not contiguous.
- Loading a path that is a directory no longer fails with an out-of-memory
  error. `AnalysisContext::from_dex`, `from_dex_files`, `from_apk`,
  `apk::Apk::open` and `raw::parser::parse_file` read the size of the
  "file", got a meaningless value, and tried to allocate that much memory.
  They now return an error. The new error code `AnalysisError::Code::CannotRead`
  means "the path exists but cannot be read"; `FileNotFound` now means only
  that the path does not exist.
- `model::read_file` no longer lets a `std::ios_base::failure` exception
  escape for a path it can open but not read, such as a directory. It returns
  an `Io` error.
- Every loader now reads its file through one function,
  `raw::read_whole_file`, which does not trust a file size it did not read.
- `model::write_file` (and `Model.write_file` in Python,
  `dexpp_model_write_file` in C, `Model.WriteFile` in Go) could report success
  when the file was not written, for example on a full disk: the error showed
  up only when the file was closed, and that result was not checked. It is now
  reported as `CannotWrite` (an `OSError` in Python).
- C API: a function that returns an empty string no longer passes a null
  pointer to `memcpy`, which is undefined behaviour. `dexpp_class_package` did
  this for every class in the default package.
- A class defined in more than one loaded DEX file now gets one answer
  everywhere: the first definition, in load order (for an APK, `classes.dex`,
  then `classes2.dex`, and so on). This is what Android does: an app's class
  loader searches its DEX files in that order and stops at the first one that
  defines the class (`DexPathList.findClass`, and
  `ClassLinker::FindClassInBaseDexClassLoaderClassPath` in ART). Before,
  `find_class` returned the first definition, the call graph's `method_for`
  returned the last one and added the edges of every definition's code, and
  the class hierarchy took the superclass of the last definition and the
  interfaces of all of them. Now the call graph, the class hierarchy and the
  cross-references use only the first definition, as `find_class` already
  did: a later definition adds no node, no edge, no supertype and no
  cross-reference. `classes()` still lists every definition. The call-graph
  documentation said "the last definition wins, as on Android"; that was
  wrong, and it now describes the rule above.
- A boolean or integer manifest attribute given by a resource reference, such
  as `android:exported="@bool/is_exported"` or
  `android:debuggable="@bool/debug"`, read as "not set". For `exported` that
  was a security problem: `is_exported` then fell back to the intent-filter
  rule, so `is_reachable` could call an exported component unreachable.
  `Apk::manifest()` now resolves such references through the APK's resource
  table, in its default configuration, for `exported`, `debuggable`,
  `allowBackup`, `versionCode`, `minSdkVersion`, `targetSdkVersion` and
  `protectionLevel`. The reference id is kept next to the value:
  `Component::exported_reference` and `Manifest::version_code_reference()`,
  `min_sdk_reference()`, `target_sdk_reference()`, `debuggable_reference()`
  and `allow_backup_reference()` (in Python `exported_reference`,
  `version_code_reference` and so on; in C the `*_reference` members of
  `DexppComponent` and `DexppManifest`, 0 for none; in Go the `...Reference`
  fields; in Java the `...Reference` record components). When a reference
  cannot be resolved (no resource table, an unknown id, a value of another
  type, or a theme attribute `?attr/...`), the value stays unset and the
  reference id is still there, so "not written" and "written, value unknown"
  can be told apart. `Manifest` takes the resource table as a new, optional
  constructor argument; built from an `XmlDocument` alone, it resolves
  nothing and keeps the reference ids. Text attributes (`android:name`,
  `android:permission`, `android:versionName`) are still read as written.
  Breaking:
  - `is_exported()` (and `is_reachable()`) now answer true when
    `android:exported` is a reference that could not be resolved, instead of
    guessing from the intent filters: the value is unknown, and "possibly
    exported" is the safe answer for triage.
  - `declared_permission_protection_level()` now returns no value for a
    declared permission whose level is an unresolved reference, and such a
    permission does not make a component `guarded_by_signature_permission`.
    Before, the level read as normal (0).
  - C: `DexppComponent` and `DexppManifest` have new members, so code built
    against the old `dexpp.h` must be rebuilt.
  - Java: the `Component` and `Manifest` records have new components, so
    their constructors take more arguments.
- `ResourceTable` now resolves boolean and integer resources too:
  `resolve_bool(id)` and `resolve_int(id)` (Python `resolve_bool` and
  `resolve_int`, C `dexpp_apk_resolve_bool` and `dexpp_apk_resolve_int`, Go
  `ResourceTable.ResolveBool` and `ResolveInt`, Java
  `ResourceTable.resolveBool` and `resolveInt`). Like Android's `TypedArray`,
  both accept any integer-typed value. All the resolve functions, including
  `resolve_string`, now follow a resource whose value refers to another
  resource (`<bool name="a">@bool/b</bool>`), for up to 20 steps as Android
  does; before, `resolve_string` returned nothing for such a resource.

### C++ API

- `Class`, `Method`, `Field` and `ClassRef` now have `operator==` and a
  `std::hash` specialization, so they can be compared and used as keys of
  `std::unordered_map` and `std::unordered_set`. Two handles are equal when
  they come from the same analysis (one `AnalysisContext`, or copies of it) and
  point at the same class, method or field. Handles from two contexts that
  loaded the same file are not equal.
- `Method::annotations()`, `Field::annotations()` and `Field::initial_value()`
  (and the same calls in Python, C and Go) no longer go through every class of
  the DEX file to find the member's class. Each DEX file now builds a table
  from type index to class the first time one of them is called, so the cost
  of a call no longer grows with the size of the file. The results do not
  change.

### C++ API (breaking)

- One error type for the whole C++ library. `dex::AnalysisError`,
  `dex::model::Error`, `dex::axml::AxmlError` and `dex::apk::ResourceError`
  are replaced by `dex::Error` (in `common/error.hpp`), with three fields:
  `code`, a `dex::ErrorCode`; `message`; and `problems`, which lists the
  broken rules for an invalid model and is empty otherwise. It comes back from
  every function that can fail: the analysis and APK loaders
  (`AnalysisContext::from_*`, `apk::Apk::open`, `apk::Apk::analysis`), the
  binary XML and resource table parsers (`axml::parse`,
  `apk::ResourceTable::parse`) and the model (`model::read`, `read_file`,
  `write`, `write_file`). So one function can load an APK and write a model
  and return a single `std::expected`. The code names match the Python
  exceptions and the C error codes. Before, the same failure had three
  names, and the AXML and resource errors had no code at all.
    - `AnalysisError::Code` becomes `ErrorCode`: `FileNotFound` and
      `InvalidApk` keep their names, `InvalidDexFile` is now `InvalidDex`,
      and `CannotRead` is new (a path that exists but cannot be read, such as
      a directory). `MalformedBytecode`, `UnresolvedReference`,
      `UnsupportedInstruction` and `InvalidDescriptor` are gone: the library
      never produced them.
    - `model::Error::kind` is now `code`, and the kinds are renamed:
      `Malformed` is `InvalidDex`, `Unsupported` is `UnsupportedDex`,
      `Invalid` is `InvalidModel` and `TooLarge` is `PoolOverflow`.
      `VersionTooLow` keeps its name. `Io` is split into `FileNotFound`
      (`read_file`: the path does not exist), `CannotRead` (`read_file`: the
      path exists but cannot be read) and `CannotWrite` (`write_file`: the
      file cannot be created or written).
    - `axml::parse` now fails with the code `InvalidXml`, and
      `apk::ResourceTable::parse` with `InvalidResources`.
    - `model::Problem` is now `dex::Problem`, since `dex::Error` holds the
      list. `model::validate()` returns `std::vector<dex::Problem>`.
    - The header `analysis/analysis_error.hpp` is removed; `dex.hpp` includes
      `common/error.hpp`.
- `dex::build_cfg` is no longer public. It took a `raw::CodeItem`, a type of
  the internal parser layer, so it was the one public function that tied users
  to that layer. `Method::cfg()` is the way to get a control-flow graph. The
  public headers no longer include any `raw/` header.
- The handles `Class`, `Method`, `Field` and `ClassRef` can no longer be built
  by hand: their constructors, which took the library's private
  implementation type, are private. Get them from the library as before
  (`AnalysisContext::classes()` and `find_class()`, `Class::methods()`,
  `fields()`, `superclass()` and `interfaces()`, `declaring_class()`,
  `CallGraph::method_for()`, `Xrefs::referrer_method()`). The public
  constructor `ClassRef(descriptor, const AnalysisContext *)` is removed too;
  use `AnalysisContext::find_class(descriptor)` to look a class up.
- There is no null `Class` any more. `ClassRef::resolve()` returns
  `std::optional<Class>`, `std::nullopt` when the class is not loaded, the
  same as `AnalysisContext::find_class()`. Before, it returned a
  default-constructed `Class` whose `is_valid()` was false. `Class` has no
  default constructor and no `is_valid()` now, so every `Class` points at a
  class. The bindings do not change: Python still gives `None`, C `NULL`, Go
  `nil` and Java an empty `Optional`.
- `TypeDescriptor` is removed. `Method::return_type()`, `Field::type()` and
  `ParameterRegister::type` now give a `std::string_view`, and
  `Method::parameters()` a `std::vector<std::string_view>`, like every other
  descriptor in the API (`Class::descriptor()`, `TypeUse::descriptor`, the
  class hierarchy) and like the Python, C, Go and Java bindings, which always
  gave plain strings. The views stay valid as long as the context, or any
  handle from it, is alive. `TypeDescriptor` had a public constructor that
  took any `std::string_view`, so it was easy to build one over a temporary
  string that then went away, and `as_class_ref()` asked for the context
  again through a raw pointer. Code that used its helpers can test the string
  itself: `d.front() == '['` for `is_array()`, `d.substr(1)` for
  `element_type()`, and `AnalysisContext::find_class(d)` instead of
  `as_class_ref(&ctx)->resolve()`.
- `dex::build_call_graph`, `dex::build_xrefs` and `dex::build_class_hierarchy`
  are no longer public. They took the library's private implementation type,
  so no user could call them. `AnalysisContext::call_graph()`, `xrefs()` and
  `class_hierarchy()` are the way to get these, as before.
- `dex::invoke_kind_from_opcode` and `dex::type_ref_kind_from_opcode` are no
  longer public. They map a raw opcode byte to an `InvokeKind` or a
  `TypeUseKind`, and the API already gives those kinds resolved:
  `MethodCall::kind`, `TypeUse::kind`, `CallEdge::kind` and the kinds of the
  cross-reference sites. No binding exposed them.
- In the parser layer, `raw::ParseError::Code::NullPointer`, which
  `raw::parser::parse_file` used for a file it could not open, is replaced by
  `FileNotFound` and `CannotRead`.

### Build

- dex++'s own sources are now compiled with `-Wall -Wextra`, and the warnings
  GCC 13 and Clang 19 reported are fixed. The new CMake option
  `DEXPP_WERROR=ON` turns a warning into an error. It is off by default, so a
  newer compiler that warns about something new does not break a build from
  source; CI builds with it on.
- The C API now has its own tests (`tests/c/`), which call every function of
  `dexpp.h` and `dexpp_model.h`. They are part of the unit tests, so the
  sanitizer run reports a leak or a double free in the C layer.

## 0.2.0

### Editable DEX model (new)

`dex::model` (include `model/model.hpp`, or `dex.hpp`) reads a DEX file into
plain C++ structs, lets you change them or build new ones from scratch, and
writes a valid DEX file again.

- The model holds no pool indices and no byte offsets. References are written
  out in full (type descriptors, prototypes, `MethodId`, `FieldId`) and
  branches point at labels. `write()` collects the references, sorts and
  numbers the pools, and picks each instruction's encoding (`Const` becomes
  `const/4`, `const/16`, `const/high16` or `const`; a branch grows to
  `goto/16`, `goto/32`, or a reversed `if` around a `goto/32` when it does not
  reach). So splitting a file, merging files, renaming classes and generating
  code are ordinary edits followed by `write()`.
- `read()` / `read_file()`, `write()` / `write_file()`, `validate()` (the
  rules of ART's DEX file verifier that apply, plus what the encoding needs),
  `required_version()`, `pool_stats()` and `rename_types()` (every type
  reference, including the class names inside generic signatures).
- Strings keep unpaired surrogates (as WTF-8), floats keep their exact bits,
  and values keep their exact type (a `char` stays a `char`), so a file
  survives a read and a write unchanged in meaning. Writing is deterministic,
  and writing a re-read output gives the same bytes again.
- Checked on 776 DEX files from 253 real apps: all but one round-trip (the
  exception is a corrupt file that ART also rejects), and ART's verifier
  (through the SDK's `dexdump`) accepts every written file.
- Speed and memory on the largest of them (42 MB, 6.9M instructions): read in
  0.6 s, write in 1.6 s; the model takes about 15 times the file size.

From Python and Go, the model works a whole class at a time. Building or
editing a class instruction by instruction stays in C++.

- Python: `dexpp.Model` reads a DEX file (`Model.read`, `Model.read_file`) or
  starts empty (`Model(version)`). It lists and inspects classes
  (`class_descriptors`, `class_info`, `class_interfaces`), copies, moves and
  removes whole classes between models (`copy_class`, `move_class`,
  `remove_class`), renames types (`rename_types`), and checks and writes the
  result (`validate`, `pool_stats`, `required_version`, `write`,
  `write_file`). Errors raise `FileNotFoundError` or `ValueError`.
- C and Go: `bindings/c/dexpp_model.h` gives the same operations on an opaque
  `DexppModelDex` handle, and the Go package wraps them as `Model`
  (`ReadModel`, `ReadModelFile`, `NewModel`, `CopyClass`, `MoveClass`,
  `RemoveClass`, `RenameTypes`, `Validate`, `PoolStats`, `Write`, ...).

### Packaging

- dexpp is now on PyPI: `pip install dexpp`. The wheels are also still
  attached to each GitHub release.
- dexpp now needs Python 3.14 or newer (breaking: 0.1.0 worked with 3.9 and
  newer). The wheels are built for CPython 3.14 only.

### Smaller changes

- `MethodId`, `FieldId` (and their hashes), `AccessFlags` and
  `AnnotationVisibility` moved to `src/common/`, so the analysis layer and
  the model share them. Their names and namespace are unchanged, and the old
  headers still work. `AccessFlags` gained `Bridge` and `Varargs`.
- `dex::raw` gained what writing needs: one opcode table for all 256 opcodes
  with an instruction decoder and encoder (`raw/opcodes.hpp`; the analysis
  decoder now uses it too), lossless MUTF-8 decoding and encoding
  (`raw/mutf8.hpp`), name and descriptor rules (`raw/descriptors.hpp`),
  SHA-1 / SHA-256 (`raw/digest.hpp`, moved from APK signing), a debug-info
  parser (`parse_debug_info`), and a byte writer (`raw/byte_writer.hpp`).
- `raw::parser::compute_signature` is now implemented. `verify_offsets`, which
  was declared but never implemented, is removed.
- `compute_checksum` is several times faster.
- The raw parser no longer negates `INT32_MIN` (undefined behavior) when a
  catch handler declares that size.

### Call graph: virtual dispatch is stored once per method (breaking)

A virtual or interface call can reach every override of the called method in
the app. These possible targets depend only on the called method, but the
call graph used to store them again at every call site, as one edge per site
and per target. On large apps that came to tens or hundreds of millions of
edges, and on the largest (for example the Facebook APK, 164K classes) the
call graph ran out of memory.

They are now stored once per called method, and the graph is built with
integer ids instead of strings. It holds the same information, in far less
time and memory (building the call graph after loading the APK; peak memory
includes the loaded APK):

| App | Stored before | Stored now | Build time before → now | Peak memory before → now |
| --- | --- | --- | --- | --- |
| Facebook | 181M edges | 3.7M edges + 206K dispatch entries | ran out of memory → 0.54 s | over 10.9 GB → 1.05 GB |
| GitHub | 89M edges | 0.76M edges + 74K dispatch entries | 4.4 s → 0.13 s | 5.8 GB → 0.21 GB |
| Instagram | 89M edges | 2.3M edges + 168K dispatch entries | 8.0 s → 0.36 s | 5.9 GB → 0.68 GB |
| YouTube | 31M edges | 1.4M edges + 105K dispatch entries | 3.9 s → 0.23 s | 2.8 GB → 0.46 GB |

Building the call graph also no longer decodes every instruction into the
context's instruction cache, and no longer builds the class hierarchy.

For an app split over several large DEX files, part of the build runs on one
thread per DEX file. `ApkLoadOptions::threads` (Python: `from_apk(threads=...)`)
now also sets these threads: `1` keeps the whole build on the calling thread.
The result never depends on the number of threads.

API changes, in C++ (`dex::CallGraph`) and Python (`dexpp.CallGraph`):

- `edges()` holds one `Declared` edge per call site, plus the
  `InheritedResolution` edge when there is one. `EdgeOrigin::ChaOverride`
  (`EdgeOrigin.CHA_OVERRIDE`) is removed.
- `dispatch_targets(node)` lists the overrides a virtual or interface call to
  that method may reach, and `dispatch_sources(node)` is the reverse.
  `dispatch_targets(edge)` gives the targets that apply at one call site (empty
  for super, direct and static calls). Python: `CallNode.dispatch_targets`,
  `CallNode.dispatch_sources`, `CallEdge.dispatch_targets`.
- `callees(node)` and `callers(node)` list every method a method may call, or
  be called by, with virtual dispatch included. Python: `CallNode.callees`,
  `CallNode.callers`.
- `CallNode.outgoing` / `incoming` (and `out_edges` / `in_edges` in Python) no
  longer include dispatch targets. Use `callees` / `callers` for those.
- The deprecated Python property `CallEdge.via_override` is removed.

## 0.1.0 — first release

The first public release. The API is still alpha: it may change in later
0.x releases.

### APK metadata (`dex::apk` / `dexpp.Apk`)

- Archive listing and entry extraction (stored and deflate entries; no ZIP64).
- Binary `AndroidManifest.xml` decoding: package, version, SDK levels,
  permissions, declared permissions and their protection levels, components
  with their exported / intent-filter / permission attributes, launcher
  activity, `debuggable`, `allowBackup`.
- `Component.is_reachable`: exported and not gated behind an app-declared
  `signature` permission.
- `resources.arsc` decoding: string resolution, resource names, reverse
  lookup.
- Signing scheme detection (v1, v2, v3/v3.1) and X.509 certificate details
  with SHA-1 / SHA-256 fingerprints. Signatures are identified, not verified.

### Code analysis (`dex` / `dexpp.AnalysisContext`)

- Load a single DEX, several DEX files, or every `classes*.dex` of an APK.
  Loading is lazy: the file is memory-mapped and only what a query touches is
  decoded. Multidex APKs are decompressed in parallel.
- Classes, methods, fields, access flags, annotations, and static field
  initial values.
- Decoded bytecode with mnemonics, try/catch blocks, register frames, and a
  control-flow graph per method.
- Per-method forward references: calls, field accesses, string loads, type
  uses.
- Whole-program call graph with class-hierarchy-aware virtual dispatch, a
  class hierarchy, and a cross-reference index (who calls / reads / writes /
  loads what).

### Python bindings

- One extension module with type stubs (`mypy` and `pyright`).
- The call graph, xrefs, and CFG tables are zero-copy views; call-graph nodes
  and edges can be navigated directly.
- `dexpp.__version__`.

### Packaging

- Prebuilt wheels for Linux x86_64 (glibc and musl), attached to the GitHub
  release. The C++ library and the C / Go bindings are source-only
  for now.
- No system dependencies: DEFLATE decoding uses libdeflate, compiled in.

### Robustness

- The parsers are hardened against malformed and hostile input (bounds
  checks, allocation limits against oversized counts and zip bombs).
