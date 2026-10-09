# Migrating from androguard to dexpp

`dexpp` covers androguard's static-analysis surface (APK metadata, DEX
introspection, cross-references) with a small, typed, layered API and a C++
core. This table maps common androguard calls to their `dexpp` equivalents.

A few shape differences to internalize first:

- **No god-objects.** androguard hands you `(apk, dvms, analysis)`. dexpp keeps
  the *package* (`Apk`) and the *code* (`AnalysisContext`) separate; you cross
  from one to the other with `Apk.analysis()`.
- **Properties, not `get_*()`.** `apk.get_package()` → `apk.manifest.package`.
- **Typed identities, not regex strings.** Methods/fields are addressed by
  `MethodId` / `FieldId`, not by matching name patterns.
- **`None`, not magic values.** Missing things are `None` / `[]`.

## Loading

| androguard | dexpp |
| --- | --- |
| `a, d, dx = AnalyzeAPK("app.apk")` | `apk = dexpp.Apk.open("app.apk")`; `ctx = apk.analysis()` |
| `APK("app.apk")` | `dexpp.Apk.open("app.apk")` |
| `AnalyzeDex("classes.dex")` | `dexpp.AnalysisContext.from_dex("classes.dex")` |
| (multidex handled internally) | `dexpp.AnalysisContext.from_apk("app.apk")` or `apk.analysis()` |

## Manifest (androguard `APK`)

| androguard | dexpp |
| --- | --- |
| `a.get_package()` | `apk.manifest.package` |
| `a.get_androidversion_code()` | `apk.manifest.version_code` |
| `a.get_androidversion_name()` | `apk.manifest.version_name` |
| `a.get_min_sdk_version()` | `apk.manifest.min_sdk` |
| `a.get_target_sdk_version()` | `apk.manifest.target_sdk` |
| `a.get_permissions()` | `apk.manifest.permissions` |
| `a.get_activities()` | `[c.name for c in apk.manifest.components_of(dexpp.ComponentKind.ACTIVITY)]` |
| `a.get_services()` | `apk.manifest.components_of(dexpp.ComponentKind.SERVICE)` |
| `a.get_receivers()` | `apk.manifest.components_of(dexpp.ComponentKind.RECEIVER)` |
| `a.get_providers()` | `apk.manifest.components_of(dexpp.ComponentKind.PROVIDER)` |
| `a.get_main_activity()` | `apk.manifest.launcher_activity` |
| `a.get_element("activity", "exported", name=...)` | `component.is_exported` (explicit-or-inferred) |
| `a.get_app_name()` | `apk.resolve_string(label_ref)` (see Resources) |

## Resources

| androguard | dexpp |
| --- | --- |
| `a.get_android_resources().get_resolved_strings()` | `apk.resources.resolve_string(id)` |
| resolve a `@string/...` ref | `apk.resolve_string(reference_id)` |
| resource id of `string/app_name` | `apk.resources.id_of("string", "app_name")` |
| name of a resource id | `apk.resources.name_of(id)` → `ResourceName` |

## Signing (androguard `APK`)

| androguard | dexpp |
| --- | --- |
| `a.is_signed_v1()` | `apk.signing.v1` |
| `a.is_signed_v2()` | `apk.signing.v2` |
| `a.is_signed_v3()` | `apk.signing.v3` |
| `a.is_signed()` | `apk.signing.is_signed` |
| `a.get_certificates()` | `apk.signing.certificates` |
| `cert.sha256_fingerprint` (via cryptography) | `cert.sha256_hex` |
| `cert.subject` | `cert.subject` (string), `cert.der` for the raw bytes |

> dexpp reports signing schemes and certificates; it does **not** verify
> signatures. Use `apksigner` for verification.

## Classes / methods / fields

| androguard | dexpp |
| --- | --- |
| `dx.get_classes()` | `ctx.classes` |
| `dx.find_classes("Lcom/foo/.*;")` | `[c for c in ctx.classes if c.name.startswith("Lcom/foo/")]` |
| `cls.get_methods()` | `cls.methods` |
| `meth.get_name()` | `method.name` |
| `meth.get_descriptor()` | `method.return_type`, `method.parameters` |
| `cls.get_fields()` | `cls.fields` |
| `dx.get_strings()` | `ctx.strings` |
| class is interface / abstract | `cls.is_interface` / `cls.is_abstract` |

## Cross-references

| androguard | dexpp |
| --- | --- |
| `meth.get_xref_from()` | `ctx.xrefs.method_refs(method_id)` (callers) |
| `meth.get_xref_to()` | `method.calls` (callees), plus `field_accesses` / `string_loads` / `type_uses` |
| `field.get_xref_read()` | `ctx.xrefs.field_reads(field_id)` |
| `field.get_xref_write()` | `ctx.xrefs.field_writes(field_id)` |
| `dx.find_methods(methodname="exec")` | `ctx.xrefs.method_refs_by_name("exec")` |
| string xrefs | `ctx.xrefs.string_refs("literal")` |

## Call graph

androguard's `dx.get_call_graph()` returns a networkx graph. dexpp's
`ctx.call_graph` is built in C++ and read through zero-copy views: `nodes`
and `edges` are read-only sequences, and nodes / edges are handles that
navigate directly.

| androguard | dexpp |
| --- | --- |
| `cg = dx.get_call_graph()` | `cg = ctx.call_graph` |
| `cg.nodes` / `cg.edges` | `cg.nodes` / `cg.edges` (sequence views) |
| `cg.successors(m)` | `[e.callee for e in node.out_edges]` |
| `cg.predecessors(m)` | `[e.caller for e in node.in_edges]` |
| find a method's node | `cg.find(dexpp.MethodId(cls, name, proto))` |
| node → method object | `node.method` (None for external methods) |

Virtual and interface calls fan out to every override in loaded subclasses
(`EdgeOrigin.CHA_OVERRIDE`), which on large apps is the vast majority of
edges — tens of millions. Filter with `where()`, which runs in C++, instead
of testing each edge in Python:

```python
# One edge per call site, without the class-hierarchy fan-out:
for e in cg.edges.where(origin=dexpp.EdgeOrigin.DECLARED):
    print(e.caller.id.name, "->", e.callee.id.name)
```

## Bytecode

| androguard | dexpp |
| --- | --- |
| `meth.get_instructions()` | `method.instructions` |
| `ins.get_name()` | `dexpp.opcode_name(ins.base.opcode)` |
| `ins.get_output()` / disassembly | `dexpp.to_string(ins)` |
| `meth.get_basic_blocks()` | `method.cfg.blocks` |
| try/catch | `method.try_blocks` |
| `meth.get_information()["params"]` → `(register, type)` | `method.parameter_registers` (`reg`, `type`, `is_this`, `is_wide`) |
| register frame size | `method.register_count` |

## Annotations

| androguard | dexpp |
| --- | --- |
| (manual via encoded annotations) | `cls.annotations` / `method.annotations` / `field.annotations` |
| static field initial value | `field.initial_value` |

## Worked example: list exported, unprotected components

```python
import dexpp

apk = dexpp.Apk.open("app.apk")
m = apk.manifest
for c in m.components:
    if c.is_exported and c.permission is None:
        print(c.kind, c.name)

# Who calls Runtime.exec across the whole app?
ctx = apk.analysis()
for target, sites in ctx.xrefs.method_refs_by_name("exec"):
    if target.class_descriptor == "Ljava/lang/Runtime;":
        for s in sites:
            print(ctx.xrefs.referrer_id(s.referrer).name, "->", target.name)
```
